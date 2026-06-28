#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sys/time.h>

#include "protocol.hpp"

using namespace std;

// socket config - no read block
void configureTimeout(int sockfd) {
	struct timeval tv;
	tv.tv_sec = TIMEOUT_SECONDS;
	tv.tv_usec = 0;
	setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// for big msg
bool sendMessageUDP(int sockfd, sockaddr_in &destAddr, int sequence, const string &data) {
	// build
	string msgStr = buildMessage(sequence, data);
	vector<string> fragments = fragmentMessage(msgStr);
	socklen_t destLen = sizeof(destAddr);

	cout << "[CLIENT] sending msg SEQ " << sequence << " in " << fragments.size() << " fragments...\n";

	/*
	for (int i = 0; i < fragments.size(); i++) {
		bool ackReceived = false;
		int intentos = 0;

		while (!ackReceived) {
			intentos++;

			string datagram = buildDatagram(sequence, i, fragments.size(), fragments[i]);

			cout << "[CLIENT] fragment " << (i + 1) << " / " << fragments.size() << "...\n";

			// =========================================================
			// test start
			if (i == 1 && intentos == 1)
				// timeout test
				cout << "   >> [TEST] frag 1 loss - timeout 1s...\n";
			else if (i == 2 && intentos == 1) {
				// wrong hash
				cout << "   >> [TEST] frag 2 corrupt...\n";
				datagram[DG_DATA_OFF] = 'X';
				sendto(sockfd, datagram.data(), datagram.size(), 0, (sockaddr*)&destAddr, destLen);
			}
			else
				sendto(sockfd, datagram.data(), datagram.size(), 0, (sockaddr*)&destAddr, destLen);
			// test end
			// =========================================================

			char buffer[UDP_PACKET_SIZE];
			int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, nullptr, nullptr);

			// first char read
			if (n > 0 && buffer[0] == TYPE_ACK) {
				int ackSeq, ackFrag; char ackStatus;
				if (extractACK(string(buffer, n), ackSeq, ackFrag, ackStatus)) {
					if (ackSeq == sequence && ackFrag == i && ackStatus == ACK_OK)
						ackReceived = true;
				}
			} else {
				if (n < 0)
					cout << "[CLIENT] Timeout - ressend fragment " << (i + 1) << " / " << fragments.size() << "...\n";
			}
		}
	}
	*/

	// send packets - waiting ok ACK
	for (int i = 0; i < fragments.size(); i++) {
		bool ackReceived = false;

		while (!ackReceived) {
			// build datagram
			string datagram = buildDatagram(sequence, i, fragments.size(), fragments[i]);

			cout << "[CLIENT] sending packet " << (i + 1) << " / " << fragments.size() << "...\n";

			sendto(sockfd, datagram.data(), datagram.size(), 0, (sockaddr*)&destAddr, destLen);

			// wait response
			char buffer[UDP_PACKET_SIZE];
			int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, nullptr, nullptr);

			// check
			if (n > 0 && buffer[0] == TYPE_ACK) {
				int ackSeq, ackFrag; char ackStatus;

				if (extractACK(string(buffer, n), ackSeq, ackFrag, ackStatus)) {
					if (ackSeq == sequence && ackFrag == i && ackStatus == ACK_OK)
						ackReceived = true; // confirmed packet
				}
			}
			else if (n < 0) {
				// timeout
				cout << "[CLIENT] timeout resend packet " << (i + 1) << " / " << fragments.size() << "...\n";
			}
		}
	}

	// waiting double ACK
	cout << "[CLIENT] all packets send. waiting ACK_COMPLETE...\n";
	bool completeAckReceived = false;

	while (!completeAckReceived) {
		char buffer[UDP_PACKET_SIZE];
		int n = recvfrom(sockfd, buffer, sizeof(buffer), 0, nullptr, nullptr);

		if (n > 0 && buffer[0] == TYPE_ACK) {
			int ackSeq, ackFrag; char ackStatus;
			if (extractACK(string(buffer, n), ackSeq, ackFrag, ackStatus)) {
				if (ackSeq == sequence && ackStatus == ACK_COMPLETE) {
					completeAckReceived = true;
					cout << "[CLIENT] receive ACK_COMPLETE...\n";
				}
			}
		} else {
			// final timeout ? send last packet again
			string lastDatagram = buildDatagram(sequence, fragments.size()-1, fragments.size(), fragments.back());
			sendto(sockfd, lastDatagram.data(), lastDatagram.size(), 0, (sockaddr*)&destAddr, destLen);
		}
	}

	return true;
}

int main() {
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	configureTimeout(sockfd); // timeout

	sockaddr_in serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(45000);
	inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

	string misPesos;

	for(int i = 0; i < 100; i++)
		misPesos += "1.523,0.432,-0.992,2.114,0.001;";
	cout << misPesos.size() << endl;

	sendMessageUDP(sockfd, serverAddr, 1, misPesos);

	close(sockfd);
	return 0;
}
