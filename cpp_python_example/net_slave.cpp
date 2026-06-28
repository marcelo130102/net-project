#include <iostream>
#include <cstring>
#include <algorithm>

#include "net_slave.hpp"
#include "protocol.hpp"

NetSlave::NetSlave(const std::string& ip, int port) : tx_sequence(0), my_slave_idx(-1) {
	sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	std::memset(&master_addr, 0, sizeof(master_addr));
	master_addr.sin_family = AF_INET;
	master_addr.sin_port = htons(port);
	inet_pton(AF_INET, ip.c_str(), &master_addr.sin_addr);

	struct timeval tv;
	tv.tv_sec = TIMEOUT_SECONDS;
	tv.tv_usec = 0;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	std::cout << "[C++] slave trying register...\n";

	// handshake
	// send R until get K + id
	bool registrado = false;
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
		}
	}
}

NetSlave::~NetSlave() {
	close(sockfd);
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

	for (int i = 0; i < fragments.size(); i++) {
		bool ackReceived = false;
		while (!ackReceived) {
			std::string datagram = buildDatagram(proto_seq, i, fragments.size(), fragments[i]);
			sendto(sockfd, datagram.data(), datagram.size(), 0, (sockaddr*)&master_addr, masterLen);

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

	bool completeAckReceived = false;
	while (!completeAckReceived) {
		char buffer[UDP_PACKET_SIZE];
		int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, nullptr, nullptr);
		if (n > 0 && buffer[0] == TYPE_ACK) {
			int ackSeq, ackFrag; char ackStatus;
			if (extractACK(std::string(buffer, n), ackSeq, ackFrag, ackStatus)) {
				if (ackSeq == proto_seq && ackStatus == ACK_COMPLETE)
					completeAckReceived = true;
			}
		} else {
			std::string lastDatagram = buildDatagram(proto_seq, fragments.size()-1, fragments.size(), fragments.back());
			sendto(sockfd, lastDatagram.data(), lastDatagram.size(), 0, (sockaddr*)&master_addr, masterLen);
		}
	}
}

py::array_t<float> NetSlave::receive_matrix() {
	socklen_t masterLen = sizeof(master_addr);
	while (true) {
		char buffer[UDP_PACKET_SIZE];
		int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, nullptr, nullptr);

		if (n <= 0)
			continue;

		if (buffer[0] == TYPE_DATAGRAM) {
			int seq, frag, tot;
			std::string payload;
			std::string packet(buffer, n);

			if (extractDatagram(packet, seq, frag, tot, payload)) {
				if (std::find(recentlyCompleted.begin(), recentlyCompleted.end(), seq) != recentlyCompleted.end()) {
					std::string finalAck = buildACK(seq, 0, ACK_COMPLETE);
					sendto(sockfd, finalAck.data(), finalAck.size(), 0, (sockaddr*)&master_addr, masterLen);
					continue;
				}

				if (!isDuplicate(seq, frag))
					storeFragment(seq, frag, tot, payload);

				std::string ack = buildACK(seq, frag, ACK_OK);
				sendto(sockfd, ack.data(), ack.size(), 0, (sockaddr*)&master_addr, masterLen);

				if (messageComplete(seq)) {
					std::string fullMsg = rebuildMessage(seq);
					recentlyCompleted.push_back(seq);

					int msgSeq; std::string data;
					if (extractMessage(fullMsg, msgSeq, data)) {
						std::string finalAck = buildACK(seq, 0, ACK_COMPLETE);
						sendto(sockfd, finalAck.data(), finalAck.size(), 0, (sockaddr*)&master_addr, masterLen);

						int rows = 0, cols = 0;
						std::memcpy(&rows, &data[0], sizeof(int));
						std::memcpy(&cols, &data[4], sizeof(int));

						auto result = py::array_t<float>({rows, cols});
						float* ptr = static_cast<float*>(result.request().ptr);
						std::memcpy(ptr, &data[8], rows * cols * sizeof(float));
						return result;
					}
				}
			}
		}
	}
}
