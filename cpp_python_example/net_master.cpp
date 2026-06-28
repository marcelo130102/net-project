#include <iostream>
#include <cstring>
#include <stdexcept>
#include <algorithm>

#include "net_master.hpp"
#include "protocol.hpp"

NetMaster::NetMaster(int port, int num_slaves) : num_slaves(num_slaves), tx_sequence(0) {
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	int opt = 1;
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	struct sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(port);

	if (bind(sockfd, (struct sockaddr*)&address, sizeof(address)) < 0)
		throw std::runtime_error("Fail to bind master port UDP");

	// timeout config
	struct timeval tv;
	tv.tv_sec = TIMEOUT_SECONDS;
	tv.tv_usec = 0;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	std::cout << "[C++] master waiting register of " << num_slaves << " slaves...\n";

	// handshake
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
				std::cout << "[C++] slave " << client_addrs.size() << " connected...\n";
			}

			// send response K + slave id
			char ack_buf[5];
			ack_buf[0] = 'K';
			int assigned_idx = client_addrs.size() - 1;
			std::memcpy(&ack_buf[1], &assigned_idx, sizeof(int));
			sendto(sockfd, ack_buf, 5, 0, (sockaddr*)&client_addr, addr_len);
		}
	}
}

NetMaster::~NetMaster() {
	close(sockfd);
}

void NetMaster::send_matrix(int slave_idx, py::array_t<float> matrix) {
	py::buffer_info buf = matrix.request();
	int rows = buf.shape[0];
	int cols = buf.shape[1];
	float* ptr = static_cast<float*>(buf.ptr);

	// serialize matrix - binary string
	// [rows(4B)][cols(4B)][data(Var)]
	int data_bytes = rows * cols * sizeof(float);
	std::string serialized_data(8 + data_bytes, 0);
	std::memcpy(&serialized_data[0], &rows, sizeof(int));
	std::memcpy(&serialized_data[4], &cols, sizeof(int));
	std::memcpy(&serialized_data[8], ptr, data_bytes);

	tx_sequence++;
	// codify slave_idx - avoid collisions
	int proto_seq = (tx_sequence << 8) | (slave_idx & 0xFF);

	std::string msgStr = buildMessage(proto_seq, serialized_data);
	std::vector<std::string> fragments = fragmentMessage(msgStr);
	sockaddr_in destAddr = client_addrs[slave_idx];
	socklen_t destLen = sizeof(destAddr);

	std::cout << "[C++] sending matrix to slave " << (slave_idx + 1) << " (" << fragments.size() << " packets)...\n";

	for (int i = 0; i < fragments.size(); i++) {
		bool ackReceived = false;
		while (!ackReceived) {
			std::string datagram = buildDatagram(proto_seq, i, fragments.size(), fragments[i]);
			sendto(sockfd, datagram.data(), datagram.size(), 0, (sockaddr*)&destAddr, destLen);

			char buffer[UDP_PACKET_SIZE];
			int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, nullptr, nullptr);

			if (n > 0 && buffer[0] == TYPE_ACK) {
				int ackSeq, ackFrag; char ackStatus;
				if (extractACK(std::string(buffer, n), ackSeq, ackFrag, ackStatus)) {
					if (ackSeq == proto_seq && ackFrag == i && ackStatus == ACK_OK)
						ackReceived = true;
				}
			}
		}
	}

	// wait final ACK_COMPLETE
	bool completeAckReceived = false;
	while (!completeAckReceived) {
		char buffer[UDP_PACKET_SIZE];
		int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, nullptr, nullptr);
		if (n > 0 && buffer[0] == TYPE_ACK) {
			int ackSeq, ackFrag; char ackStatus;
			if (extractACK(std::string(buffer, n), ackSeq, ackFrag, ackStatus)) {
				if (ackSeq == proto_seq && ackStatus == ACK_COMPLETE) {
					completeAckReceived = true;
				}
			}
		} else {
			std::string lastDatagram = buildDatagram(proto_seq, fragments.size()-1, fragments.size(), fragments.back());
			sendto(sockfd, lastDatagram.data(), lastDatagram.size(), 0, (sockaddr*)&destAddr, destLen);
		}
	}
}

py::array_t<float> NetMaster::receive_matrix(int slave_idx) {
	// slave matrix already saved
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

	while (true) {
		char buffer[UDP_PACKET_SIZE];
		sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);
		int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr*)&client_addr, &client_len);

		if (n <= 0)
			continue;

		if (buffer[0] == TYPE_DATAGRAM) {
			int seq, frag, tot;
			std::string payload;
			std::string packet(buffer, n);

			if (extractDatagram(packet, seq, frag, tot, payload)) {
				int sender_idx = seq & 0xFF; // extract sender slave

				if (std::find(recentlyCompleted.begin(), recentlyCompleted.end(), seq) != recentlyCompleted.end()) {
					std::string finalAck = buildACK(seq, 0, ACK_COMPLETE);
					sendto(sockfd, finalAck.data(), finalAck.size(), 0, (sockaddr*)&client_addr, client_len);
					continue;
				}

				if (!isDuplicate(seq, frag))
					storeFragment(seq, frag, tot, payload);

				std::string ack = buildACK(seq, frag, ACK_OK);
				sendto(sockfd, ack.data(), ack.size(), 0, (sockaddr*)&client_addr, client_len);

				if (messageComplete(seq)) {
					std::string fullMsg = rebuildMessage(seq);
					recentlyCompleted.push_back(seq);

					int msgSeq; std::string data;
					if (extractMessage(fullMsg, msgSeq, data)) {
						std::string finalAck = buildACK(seq, 0, ACK_COMPLETE);
						sendto(sockfd, finalAck.data(), finalAck.size(), 0, (sockaddr*)&client_addr, client_len);

						// save cache with slave id
						completed_matrices[sender_idx] = data;

						// if correct slave - process
						if (sender_idx == slave_idx) {
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
					}
				}
			}
		}
	}
}
