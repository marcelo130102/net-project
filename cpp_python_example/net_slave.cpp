#include <iostream>
#include <cstring>
#include <algorithm>

#include "net_slave.hpp"
#include "protocol.hpp"

// --- FLAGS GLOBALES PARA PRUEBAS (Cámbialos a true para probar) ---
bool test_loss_slave = true;
bool test_corrupt_slave = true;
bool test_timeout_slave = true;



NetSlave::NetSlave(const std::string& ip, int port) : tx_sequence(0), my_slave_idx(-1) {
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sockfd < 0) {
		throw std::runtime_error("Failed to create UDP socket");
	}

	std::memset(&master_addr, 0, sizeof(master_addr));
	master_addr.sin_family = AF_INET;
	master_addr.sin_port = htons(port);
	inet_pton(AF_INET, ip.c_str(), &master_addr.sin_addr);

	struct timeval tv;
	tv.tv_sec = TIMEOUT_SECONDS;
	tv.tv_usec = 0;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	std::cout << "[C++] slave trying register...\n";

	// Handshake
	bool registrado = false;
	int attempts = 0;
	while (!registrado) {
		char reg_buf[1] = {'R'};
		sendto(sockfd, reg_buf, 1, 0, (sockaddr*)&master_addr, sizeof(master_addr));

		char ack_buf[5];
		sockaddr_in from_addr;
		socklen_t from_len = sizeof(from_addr);
		int n = recvfrom(sockfd, ack_buf, sizeof(ack_buf), 0, (sockaddr*)&from_addr, &from_len);

		if (n >= 5 && ack_buf[0] == 'K') {
			std::memcpy(&my_slave_idx, &ack_buf[1], sizeof(int));
			registrado = true;
			std::cout << "[C++] slave registered. assigned slave_idx: " << my_slave_idx << "...\n";
		} else {
			attempts++;
			if (attempts % 5 == 0) {
				std::cout << "[C++] registration attempt " << attempts << " timed out, retrying...\n";
			}
		}
	}
}

NetSlave::~NetSlave() {
	if (sockfd >= 0) {
		close(sockfd);
	}
}

void NetSlave::send_matrix(py::array_t<float> matrix) {
	py::buffer_info buf = matrix.request();
	int rows = buf.shape[0];
	int cols = buf.shape[1];
	float* ptr = static_cast<float*>(buf.ptr);

	int data_bytes = rows * cols * sizeof(float);
	std::string serialized_data(8 + data_bytes, 0);
	std::memcpy(&serialized_data[0], &rows, sizeof(int));
	std::memcpy(&serialized_data[4], &cols, sizeof(int));
	std::memcpy(&serialized_data[8], ptr, data_bytes);

	tx_sequence++;
	int proto_seq = (tx_sequence << 8) | (my_slave_idx & 0xFF);

	std::string msgStr = buildMessage(proto_seq, serialized_data);
	std::vector<std::string> fragments = fragmentMessage(msgStr);
	socklen_t masterLen = sizeof(master_addr);

	// Release GIL for blocking I/O loop
	{
		py::gil_scoped_release release;

		resetBackoff(metrics);

		for (size_t i = 0; i < fragments.size(); i++) {
			bool ackReceived = false;
			while (!ackReceived) {
				applySocketTimeout(sockfd, metrics.timeout);

				std::string datagram = buildDatagram(proto_seq, i, fragments.size(), fragments[i]);
				auto timeStart = std::chrono::steady_clock::now();


				// ==========================================
				bool enviarPaqueteNormal = true;
				

				static bool perdidaInyectada = false;

				if (test_loss_slave && i == 1 && metrics.backoffCount == 0 && !perdidaInyectada) {
					std::cout << "   >> [TEST PÉRDIDA SLAVE] Tirando fragmento " << (i + 1) << " a la basura para forzar Timeout...\n";
					enviarPaqueteNormal = false; 
					perdidaInyectada = true; // Se vuelve true y nunca más volverá a entrar aquí
				}
				
				static bool corromperFragmento = true;
				if (test_corrupt_slave && i == 3 && corromperFragmento) {
					std::cout << "   >> [TEST CORRUPCIÓN SLAVE] Alterando bits del fragmento " << (i + 1) << " para romper el CRC...\n";
					datagram[DG_DATA_OFF] ^= 0xFF;
					corromperFragmento = false;
				}

				if (enviarPaqueteNormal) {
					sendto(sockfd, datagram.data(), datagram.size(), 0, (sockaddr*)&master_addr, masterLen);
				}

				char buffer[UDP_PACKET_SIZE];
				memset(buffer, 0, UDP_PACKET_SIZE);
				int n = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, nullptr, nullptr);

				// ==========================================
				static bool ignorarAckUnaVez = true;
				if (test_timeout_slave && i == 5 && ignorarAckUnaVez && n > 0) {
					std::cout << "   >> [TEST ACK PERDIDO SLAVE] Ignorando el ACK del fragmento " << (i + 1) << " para obligar a retransmitir...\n";
					ignorarAckUnaVez = false;
					n = -1; 
				}

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
								// NACK
								std::cout << "[C++] <<NACK>> received due to master CRC error. Retransmitting fragment " << (i + 1) << "...\n";
							}
						}
					}
				} else {
					if (n < 0) {
                        applyBackoff(metrics);
/*						 // --- Limite
                        if (metrics.backoffCount > 5) {
                            throw std::runtime_error("Master desconectado. Limite de reintentos de envio superado.");
                        }
                        // --------------------------------------------
*/
                        if (metrics.timeout > 2.0) {
                            metrics.timeout = 2.0;
                        }
                        std::cout << "[C++] Timeout. Retransmitting fragment " << (i + 1) << " with backoff. Timeout: " << metrics.timeout * 1000.0 << " ms...\n";
                    }
				}
			}
		}

		bool completeAckReceived = false;
		while (!completeAckReceived) {
			applySocketTimeout(sockfd, metrics.timeout);

			char buffer[UDP_PACKET_SIZE];
			memset(buffer, 0, UDP_PACKET_SIZE);
			int n = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, nullptr, nullptr);
			if (n == UDP_PACKET_SIZE && buffer[0] == TYPE_ACK) {
				int ackSeq, ackFrag; char ackStatus;
				if (extractACK(std::string(buffer, UDP_PACKET_SIZE), ackSeq, ackFrag, ackStatus)) {
					if (ackSeq == proto_seq && ackStatus == ACK_COMPLETE)
						completeAckReceived = true;
				}
			} else {
				std::string lastDatagram = buildDatagram(proto_seq, fragments.size() - 1, fragments.size(), fragments.back());
				sendto(sockfd, lastDatagram.data(), lastDatagram.size(), 0, (sockaddr*)&master_addr, masterLen);
			}
		}
	}
}

py::array_t<float> NetSlave::receive_matrix() {
	socklen_t masterLen = sizeof(master_addr);
	std::string serialized_data;
	bool found = false;

	// Release GIL for blocking I/O loop
	{
		py::gil_scoped_release release;
		applySocketTimeout(sockfd, 1.0);
	int timeout_counter = 0; // CONTADOR
		while (!found) {
			cleanupZombieMessages();

			char buffer[UDP_PACKET_SIZE];
			memset(buffer, 0, UDP_PACKET_SIZE);
			int n = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, nullptr, nullptr);
			if (n <= 0) {
	
                timeout_counter++;
                if (timeout_counter > 10) { // Aprox 10 segundos inactivos
                    throw std::runtime_error("Master desconectado. Tiempo de espera de recepcion agotado.");
                }
  
				continue;
			}
			timeout_counter = 0; 

			if (n == UDP_PACKET_SIZE && buffer[0] == TYPE_DATAGRAM) {
				int seq, frag, tot;
				std::string payload;
				std::string packet(buffer, UDP_PACKET_SIZE);

				if (extractDatagram(packet, seq, frag, tot, payload)) {
					std::string msgKey = std::to_string(master_addr.sin_addr.s_addr) + ":" + 
					                     std::to_string(master_addr.sin_port) + ":" + std::to_string(seq);

					if (std::find(recentlyCompleted.begin(), recentlyCompleted.end(), msgKey) != recentlyCompleted.end()) {
						std::string finalAck = buildACK(seq, 0, ACK_COMPLETE);
						sendto(sockfd, finalAck.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&master_addr, masterLen);
						continue;
					}

					if (!isDuplicate(msgKey, frag)) {
						storeFragment(msgKey, frag, tot, payload);
					}

					std::string ack = buildACK(seq, frag, ACK_OK);
					sendto(sockfd, ack.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&master_addr, masterLen);

					if (messageComplete(msgKey)) {
						std::string fullMsg = rebuildMessage(msgKey);
						recentlyCompleted.push_back(msgKey);
						if (recentlyCompleted.size() > 100) {
							recentlyCompleted.erase(recentlyCompleted.begin());
						}

						int msgSeq; std::string data;
						if (extractMessage(fullMsg, msgSeq, data)) {
							std::string finalAck = buildACK(seq, 0, ACK_COMPLETE);
							sendto(sockfd, finalAck.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&master_addr, masterLen);

							serialized_data = data;
							found = true;
						}
					}
				} else {
					// CRC Validation failed. Send NACK
					int netBadSeq = 0, netBadFrag = 0;
					std::memcpy(&netBadSeq, &buffer[DG_SEQ_OFF], sizeof(int));
					std::memcpy(&netBadFrag, &buffer[DG_FRAG_OFF], sizeof(int));
					int badSeq = ntohl(netBadSeq);
					int badFrag = ntohl(netBadFrag);

					std::cout << "[C++] CRC error detected on fragment " << (badFrag + 1) << ". Sending NACK...\n";

					std::string nack = buildACK(badSeq, badFrag, ACK_ERROR);
					sendto(sockfd, nack.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&master_addr, masterLen);
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
