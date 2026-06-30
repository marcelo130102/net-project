#include <iostream>
#include <cstring>
#include <stdexcept>
#include <algorithm>

#include "net_master.hpp"
#include "protocol.hpp"

NetMaster::NetMaster(int port, int num_slaves) : num_slaves(num_slaves), tx_sequence(0) {
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		throw std::runtime_error("Failed to create UDP socket");
	}

	int opt = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	// Aumentar el buffer UDP a ~8MB para soportar concurrencia
	int rcvBufferSize = 8 * 1024 * 1024; // 8 MB
	if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvBufferSize, sizeof(rcvBufferSize)) < 0) {
		std::cout << "[ALERTA] No se pudo aumentar el buffer de recepcion UDP en master.\n";
	}

	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);

	if (::bind(sockfd, (struct sockaddr*)&address, sizeof(address)) < 0) {
		close(sockfd);
		throw std::runtime_error("Failed to bind master port UDP");
	}

	// Configuración de timeout por defecto
	struct timeval tv;
	tv.tv_sec = TIMEOUT_SECONDS;
	tv.tv_usec = 0;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	std::cout << "[C++] master waiting register of " << num_slaves << " slaves...\n";

	// Handshake
	while (client_addrs.size() < num_slaves) {
		char buf[10];
		sockaddr_in client_addr;
		socklen_t addr_len = sizeof(client_addr);
		int n = recvfrom(sockfd, buf, sizeof(buf), 0, (sockaddr*)&client_addr, &addr_len);

		if (n > 0 && buf[0] == 'R') { // R for register
			bool exist = false;
			for (const auto& addr : client_addrs) {
				if (addr.sin_addr.s_addr == client_addr.sin_addr.s_addr && addr.sin_port == client_addr.sin_port) {
					exist = true;
					break;
				}
			}
			if (!exist) {
				client_addrs.push_back(client_addr);
				client_metrics.push_back(RTTMetrics()); // default initialization
				std::cout << "[C++] slave " << client_addrs.size() << " connected...\n";
			}

			// send response K + slave id
			char ack_buf[5];
			ack_buf[0] = 'K';
			int assigned_idx = -1;
			for (size_t i = 0; i < client_addrs.size(); i++) {
				if (client_addrs[i].sin_addr.s_addr == client_addr.sin_addr.s_addr && client_addrs[i].sin_port == client_addr.sin_port) {
					assigned_idx = i;
					break;
				}
			}
			std::memcpy(&ack_buf[1], &assigned_idx, sizeof(int));
			sendto(sockfd, ack_buf, 5, 0, (sockaddr*)&client_addr, addr_len);
		}
	}
}

NetMaster::~NetMaster() {
	if (sockfd >= 0) {
		close(sockfd);
	}
}

void NetMaster::send_matrix(int slave_idx, py::array_t<float> matrix) {
	if (slave_idx < 0 || slave_idx >= client_addrs.size()) {
		throw std::out_of_range("Invalid slave index");
	}

	py::buffer_info buf = matrix.request();
	int rows = buf.shape[0];
	int cols = buf.shape[1];
	float* ptr = static_cast<float*>(buf.ptr);

	// Serialize matrix - binary string: [rows(4B)][cols(4B)][data(Var)]
	int data_bytes = rows * cols * sizeof(float);
	std::string serialized_data(8 + data_bytes, 0);
	std::memcpy(&serialized_data[0], &rows, sizeof(int));
	std::memcpy(&serialized_data[4], &cols, sizeof(int));
	std::memcpy(&serialized_data[8], ptr, data_bytes);

	tx_sequence++;
	// Codify slave_idx in sequence number to isolate streams
	int proto_seq = (tx_sequence << 8) | (slave_idx & 0xFF);

	std::string msgStr = buildMessage(proto_seq, serialized_data);
	std::vector<std::string> fragments = fragmentMessage(msgStr);
	sockaddr_in destAddr = client_addrs[slave_idx];
	socklen_t destLen = sizeof(destAddr);

	// Release GIL for the blocking socket I/O
	{
		py::gil_scoped_release release;

		std::cout << "[C++] sending matrix to slave " << (slave_idx + 1) << " (" << fragments.size() << " packets)...\n";

		RTTMetrics& metrics = client_metrics[slave_idx];
		resetBackoff(metrics);

		for (size_t i = 0; i < fragments.size(); i++) {
			bool ackReceived = false;
			while (!ackReceived) {
				applySocketTimeout(sockfd, metrics.timeout);

				std::string datagram = buildDatagram(proto_seq, i, fragments.size(), fragments[i]);
				auto timeStart = std::chrono::steady_clock::now();

				sendto(sockfd, datagram.data(), datagram.size(), 0, (sockaddr*)&destAddr, destLen);

				char buffer[UDP_PACKET_SIZE];
				memset(buffer, 0, UDP_PACKET_SIZE);
				int n = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, nullptr, nullptr);

				if (n == UDP_PACKET_SIZE && buffer[0] == TYPE_ACK) {
					int ackSeq, ackFrag; char ackStatus;
					if (extractACK(std::string(buffer, UDP_PACKET_SIZE), ackSeq, ackFrag, ackStatus)) {
						if (ackSeq == proto_seq && ackFrag == static_cast<int>(i)) {
							if (ackStatus == ACK_OK || ackStatus == ACK_COMPLETE) {
								auto timeEnd = std::chrono::steady_clock::now();
								double measuredRTT = std::chrono::duration_cast<std::chrono::duration<double>>(timeEnd - timeStart).count();
								updateRTT(metrics, measuredRTT);
								resetBackoff(metrics);
								ackReceived = true;
							} else if (ackStatus == ACK_ERROR) {
								// NACK: Retransmit immediately without backoff/RTO increment
								std::cout << "[C++] <<NACK>> received due to server CRC error. Retransmitting fragment " << (i + 1) << "...\n";
							}
						}
					}
				} else {
					if (n < 0) {
						// Timeout occurred
						applyBackoff(metrics);
						if (metrics.timeout > 2.0) {
							metrics.timeout = 2.0;
						}
						std::cout << "[C++] Timeout. Retransmitting fragment " << (i + 1) << " with backoff. Timeout: " << metrics.timeout * 1000.0 << " ms...\n";
					}
				}
			}
		}

		// wait final ACK_COMPLETE
		bool completeAckReceived = false;
		while (!completeAckReceived) {
			applySocketTimeout(sockfd, metrics.timeout);

			char buffer[UDP_PACKET_SIZE];
			memset(buffer, 0, UDP_PACKET_SIZE);
			int n = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, nullptr, nullptr);
			if (n == UDP_PACKET_SIZE && buffer[0] == TYPE_ACK) {
				int ackSeq, ackFrag; char ackStatus;
				if (extractACK(std::string(buffer, UDP_PACKET_SIZE), ackSeq, ackFrag, ackStatus)) {
					if (ackSeq == proto_seq && ackStatus == ACK_COMPLETE) {
						completeAckReceived = true;
					}
				}
			} else {
				// Resend last fragment to prompt ACK_COMPLETE
				std::string lastDatagram = buildDatagram(proto_seq, fragments.size() - 1, fragments.size(), fragments.back());
				sendto(sockfd, lastDatagram.data(), lastDatagram.size(), 0, (sockaddr*)&destAddr, destLen);
			}
		}
	}
}

py::array_t<float> NetMaster::receive_matrix(int slave_idx) {
	if (slave_idx < 0 || slave_idx >= client_addrs.size()) {
		throw std::out_of_range("Invalid slave index");
	}

	// If matrix already received and cached, return it immediately
	if (completed_matrices.find(slave_idx) != completed_matrices.end()) {
		std::string serialized_data = completed_matrices[slave_idx];
		completed_matrices.erase(slave_idx);

		int rows = 0, cols = 0;
		std::memcpy(&rows, &serialized_data[0], sizeof(int));
		std::memcpy(&cols, &serialized_data[4], sizeof(int));

		auto result = py::array_t<float>({rows, cols});
		float* ptr = static_cast<float*>(result.request().ptr);
		std::memcpy(ptr, &serialized_data[8], rows * cols * sizeof(float));
		return result;
	}

	std::string serialized_data;
	bool found = false;

	// Release GIL for the blocking socket I/O loop
	{
		py::gil_scoped_release release;
		applySocketTimeout(sockfd, 1.0);

		while (!found) {
			cleanupZombieMessages();

			char buffer[UDP_PACKET_SIZE];
			memset(buffer, 0, UDP_PACKET_SIZE);
			sockaddr_in client_addr;
			socklen_t client_len = sizeof(client_addr);
			int n = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, (sockaddr*)&client_addr, &client_len);

			if (n <= 0) {
				continue; // socket timeout or error, retry
			}

			if (n == UDP_PACKET_SIZE && buffer[0] == TYPE_DATAGRAM) {
				int seq, frag, tot;
				std::string payload;
				std::string packet(buffer, UDP_PACKET_SIZE);

				if (extractDatagram(packet, seq, frag, tot, payload)) {
					int sender_idx = seq & 0xFF; // extract sender slave index from sequence

					// IP:PORT:SEQ
					std::string msgKey = std::to_string(client_addr.sin_addr.s_addr) + ":" +
					                     std::to_string(client_addr.sin_port) + ":" + std::to_string(seq);

					// If recently completed, resend ACK_COMPLETE
					if (std::find(recentlyCompleted.begin(), recentlyCompleted.end(), msgKey) != recentlyCompleted.end()) {
						std::string finalAck = buildACK(seq, 0, ACK_COMPLETE);
						sendto(sockfd, finalAck.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&client_addr, client_len);
						continue;
					}

					if (!isDuplicate(msgKey, frag)) {
						storeFragment(msgKey, frag, tot, payload);
					}

					// Standard ACK
					std::string ack = buildACK(seq, frag, ACK_OK);
					sendto(sockfd, ack.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&client_addr, client_len);

					if (messageComplete(msgKey)) {
						std::string fullMsg = rebuildMessage(msgKey);
						recentlyCompleted.push_back(msgKey);
						if (recentlyCompleted.size() > 100) {
							recentlyCompleted.erase(recentlyCompleted.begin());
						}

						int msgSeq; std::string data;
						if (extractMessage(fullMsg, msgSeq, data)) {
							// Send final ACK_COMPLETE
							std::string finalAck = buildACK(seq, 0, ACK_COMPLETE);
							sendto(sockfd, finalAck.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&client_addr, client_len);

							completed_matrices[sender_idx] = data;

							if (sender_idx == slave_idx) {
								serialized_data = completed_matrices[slave_idx];
								completed_matrices.erase(slave_idx);
								found = true;
							}
						}
					}
				} else {
					// CRC Validation failed. Extract sequence and fragment without validation to emit NACK
					int netBadSeq = 0, netBadFrag = 0;
					std::memcpy(&netBadSeq, &buffer[DG_SEQ_OFF], sizeof(int));
					std::memcpy(&netBadFrag, &buffer[DG_FRAG_OFF], sizeof(int));
					int badSeq = ntohl(netBadSeq);
					int badFrag = ntohl(netBadFrag);

					std::cout << "[C++] CRC error detected on fragment " << (badFrag + 1) << ". Sending NACK...\n";

					std::string nack = buildACK(badSeq, badFrag, ACK_ERROR);
					sendto(sockfd, nack.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&client_addr, client_len);
				}
			}
		}
	}

	// Now with GIL re-acquired: reconstruct py::array_t
	int rows = 0, cols = 0;
	std::memcpy(&rows, &serialized_data[0], sizeof(int));
	std::memcpy(&cols, &serialized_data[4], sizeof(int));

	auto result = py::array_t<float>({rows, cols});
	float* ptr = static_cast<float*>(result.request().ptr);
	std::memcpy(ptr, &serialized_data[8], rows * cols * sizeof(float));
	return result;
}
