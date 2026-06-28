#include <cstring>
#include <algorithm>

#include "protocol.hpp"

using namespace std;

// save reception msg
map<int, PendingMessage> pendingMessages;

// hash
int calculateCRC(const char *buffer, int size) {
	int crc = 0;
	for(int i = 0; i < size; i++)
		crc += (unsigned char)buffer[i];
	return crc;
}

// msg
string buildMessage(int sequence, const string &data) {
	int dataSize = data.size();
	int totalSize = MSG_HEADER_SIZE + dataSize + CRC_SIZE;

	string output(totalSize, 0);

	output[MSG_TYPE_OFF] = TYPE_MESSAGE;
	memcpy(&output[MSG_SEQ_OFF], &sequence, sizeof(int));
	memcpy(&output[MSG_SIZE_OFF], &dataSize, sizeof(int));

	// copy data
	memcpy(&output[MSG_HEADER_SIZE], data.data(), dataSize);

	// calc CRC
	int crc = calculateCRC(output.data(), MSG_HEADER_SIZE + dataSize);
	memcpy(&output[MSG_HEADER_SIZE + dataSize], &crc, sizeof(int));

	return output;
}

bool extractMessage(const string &msgStr, int &sequence, string &data) {
	if(msgStr.empty() || msgStr[MSG_TYPE_OFF] != TYPE_MESSAGE)
		return false;

	memcpy(&sequence, &msgStr[MSG_SEQ_OFF], sizeof(int));

	int dataSize;
	memcpy(&dataSize, &msgStr[MSG_SIZE_OFF], sizeof(int));

	int expectedTotalSize = MSG_HEADER_SIZE + dataSize + CRC_SIZE;
	if(msgStr.size() < expectedTotalSize)
		return false; // missing data

	// check CRC
	int receivedCRC;
	memcpy(&receivedCRC, &msgStr[MSG_HEADER_SIZE + dataSize], sizeof(int));

	int calcCRC = calculateCRC(msgStr.data(), MSG_HEADER_SIZE + dataSize);
	if(receivedCRC != calcCRC)
		return false;

	// extract pure data
	data = msgStr.substr(MSG_HEADER_SIZE, dataSize);
	return true;
}

// split & join msg
vector<string> fragmentMessage(const string &message) {
	vector<string> fragments;
	int offset = 0;
	while(offset < message.size()) {
		int sizeToCopy = min(DG_MAX_PAYLOAD, (int)message.size() - offset);
		fragments.push_back(message.substr(offset, sizeToCopy));
		offset += sizeToCopy;
	}
	return fragments;
}

string rebuildMessage(int sequence) {
	string completeMsg = "";
	if(pendingMessages.find(sequence) != pendingMessages.end()) {
		for(const string &frag : pendingMessages[sequence].fragments)
			completeMsg += frag;
		pendingMessages.erase(sequence); // clean map
	}
	return completeMsg;
}

// datagram
string buildDatagram(int sequence, int fragment, int totalFragments, const string &payload) {
	string output(UDP_PACKET_SIZE, 0);

	output[DG_TYPE_OFF] = TYPE_DATAGRAM;
	memcpy(&output[DG_SEQ_OFF], &sequence, sizeof(int));
	memcpy(&output[DG_FRAG_OFF], &fragment, sizeof(int));
	memcpy(&output[DG_TOT_OFF], &totalFragments, sizeof(int));

	// copy payload - 483
	memcpy(&output[DG_DATA_OFF], payload.data(), payload.size());

	// CRC of first 496
	int crc = calculateCRC(output.data(), UDP_PACKET_SIZE - CRC_SIZE);
	memcpy(&output[UDP_PACKET_SIZE - CRC_SIZE], &crc, sizeof(int));

	return output;
}

bool extractDatagram(const string &datagram, int &sequence, int &fragment, int &totalFragments, string &payload) {
	if(datagram.size() != UDP_PACKET_SIZE)
		return false;
	if(datagram[DG_TYPE_OFF] != TYPE_DATAGRAM)
		return false;

	// check CRC
	int receivedCRC;
	memcpy(&receivedCRC, &datagram[UDP_PACKET_SIZE - CRC_SIZE], sizeof(int));
	int calcCRC = calculateCRC(datagram.data(), UDP_PACKET_SIZE - CRC_SIZE);

	if(receivedCRC != calcCRC)
		return false; // wrong hash

	// headers
	memcpy(&sequence, &datagram[DG_SEQ_OFF], sizeof(int));
	memcpy(&fragment, &datagram[DG_FRAG_OFF], sizeof(int));
	memcpy(&totalFragments, &datagram[DG_TOT_OFF], sizeof(int));

	// payload
	payload = datagram.substr(DG_DATA_OFF, DG_MAX_PAYLOAD);
	return true;
}

// ACK
string buildACK(int sequence, int fragment, char status) {
	string output(ACK_PACKET_SIZE, 0); // 14

	output[ACK_TYPE_OFF] = TYPE_ACK;
	memcpy(&output[ACK_SEQ_OFF], &sequence, sizeof(int));
	memcpy(&output[ACK_FRAG_OFF], &fragment, sizeof(int));
	output[ACK_STATUS_OFF] = status;

	int crc = calculateCRC(output.data(), ACK_PACKET_SIZE - CRC_SIZE);
	memcpy(&output[ACK_CRC_OFF], &crc, sizeof(int));

	return output;
}

bool extractACK(const string &ackStr, int &sequence, int &fragment, char &status) {
	if(ackStr.size() != ACK_PACKET_SIZE)
		return false;
	if(ackStr[ACK_TYPE_OFF] != TYPE_ACK)
		return false;

	int receivedCRC;
	memcpy(&receivedCRC, &ackStr[ACK_CRC_OFF], sizeof(int));
	int calcCRC = calculateCRC(ackStr.data(), ACK_PACKET_SIZE - CRC_SIZE);

	if(receivedCRC != calcCRC)
		return false;

	memcpy(&sequence, &ackStr[ACK_SEQ_OFF], sizeof(int));
	memcpy(&fragment, &ackStr[ACK_FRAG_OFF], sizeof(int));
	status = ackStr[ACK_STATUS_OFF];

	return true;
}

// reception
bool isDuplicate(int sequence, int fragment) {
	if(pendingMessages.find(sequence) == pendingMessages.end())
		return false;
	if(fragment >= pendingMessages[sequence].fragments.size())
		return false;

	return !pendingMessages[sequence].fragments[fragment].empty();
}

void storeFragment(int sequence, int fragment, int totalFragments, const string &payload) {
	if(pendingMessages.find(sequence) == pendingMessages.end()) {
		pendingMessages[sequence].totalFragments = totalFragments;
		pendingMessages[sequence].fragments.resize(totalFragments, "");
		pendingMessages[sequence].fragmentsReceived = 0;
	}

	if(pendingMessages[sequence].fragments[fragment].empty()) {
		pendingMessages[sequence].fragments[fragment] = payload;
		pendingMessages[sequence].fragmentsReceived++;
	}
}

bool messageComplete(int sequence) {
	if(pendingMessages.find(sequence) == pendingMessages.end())
		return false;
	return pendingMessages[sequence].fragmentsReceived == pendingMessages[sequence].totalFragments;
}
