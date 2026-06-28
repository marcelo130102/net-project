#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <algorithm>

#include "protocol.hpp"

using namespace std;

// for complete msg
vector<int> recentlyCompleted;

string receiveMessageUDP(int sockfd, sockaddr_in &clientAddr, socklen_t &clientLen) {
	while (true) {
		char buffer[UDP_PACKET_SIZE];
		int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, (sockaddr*)&clientAddr, &clientLen);

		if (n <= 0)
			continue; // keep listening

		// read first char
		if (buffer[0] == TYPE_DATAGRAM) {
			int seq, frag, tot;
			string payload;
			string packet(buffer, n);

			// check hash & ignore if wrong
			if (extractDatagram(packet, seq, frag, tot, payload)) {

				cout << "[SERVER] packet receive " << (frag + 1) << " / " << tot << "...\n";

				// same packet - lost ACK
				if (find(recentlyCompleted.begin(), recentlyCompleted.end(), seq) != recentlyCompleted.end()) {
					cout << "[SERVER] packet delay. resend ACK_COMPLETE...\n";
					string finalAck = buildACK(seq, 0, ACK_COMPLETE);
					sendto(sockfd, finalAck.data(), finalAck.size(), 0, (sockaddr*)&clientAddr, clientLen);
					continue;
				}

				// save packet
				if (!isDuplicate(seq, frag))
					storeFragment(seq, frag, tot, payload);

				// send ACK
				string ack = buildACK(seq, frag, ACK_OK);
				sendto(sockfd, ack.data(), ack.size(), 0, (sockaddr*)&clientAddr, clientLen);

				// check all packets
				if (messageComplete(seq)) {
					// rebuild
					string fullMsg = rebuildMessage(seq);
					recentlyCompleted.push_back(seq);

					int msgSeq;
					string data;

					// check full msg hash
					if (extractMessage(fullMsg, msgSeq, data)) {
						// send final ACK
						string finalAck = buildACK(seq, 0, ACK_COMPLETE);
						sendto(sockfd, finalAck.data(), finalAck.size(), 0, (sockaddr*)&clientAddr, clientLen);

						return data;
					}
				}
			}
			else {
				cout << "[SERVER] Error: wrong hash or corrupted packet. discarting...\n";
			}
		}
	}
}

int main() {
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

	sockaddr_in serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(45000);
	serverAddr.sin_addr.s_addr = INADDR_ANY;

	bind(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr));

	cout << "[SERVER] listening...\n";

	sockaddr_in clientAddr;
	socklen_t clientLen = sizeof(clientAddr);

	// to block until finish
	string mensajeLimpio = receiveMessageUDP(sockfd, clientAddr, clientLen);

	cout << "\n--- received full msg ---\n";
	cout << mensajeLimpio << "\n";
	cout << "------------------------------------\n";

	close(sockfd);
	return 0;
}
