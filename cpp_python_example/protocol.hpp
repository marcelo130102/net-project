#pragma once

#include <string>
#include <vector>
#include <map>

using namespace std;

// general config
const int UDP_PACKET_SIZE = 500;
const int ACK_PACKET_SIZE = 14;
const int TIMEOUT_SECONDS = 1;

// packets type
const char TYPE_MESSAGE  = 'M';
const char TYPE_DATAGRAM = 'D';
const char TYPE_ACK      = 'A';

// ACK states
const char ACK_ERROR     = 0; // NACK
const char ACK_OK        = 1;
const char ACK_COMPLETE  = 2; // full msg

// offsets msg - any size
// format: [TIPO(1)] [SEQ(4)] [TAMAÑO_DATA(4)] [DATA(Var)] [CRC(4)]
const int MSG_TYPE_OFF    = 0;
const int MSG_SEQ_OFF     = 1;
const int MSG_SIZE_OFF    = 5;
const int MSG_HEADER_SIZE = 9;
const int CRC_SIZE        = 4;

// offsets datagram - 500
// format: [TIPO(1)] [SEQ(4)] [FRAG(4)] [TOTAL_FRAGS(4)] [PAYLOAD(483)] [CRC(4)]
const int DG_TYPE_OFF    = 0;
const int DG_SEQ_OFF     = 1;
const int DG_FRAG_OFF    = 5;
const int DG_TOT_OFF     = 9;
const int DG_DATA_OFF    = 13;
const int DG_HEADER_SIZE = 13;
const int DG_MAX_PAYLOAD = UDP_PACKET_SIZE - DG_HEADER_SIZE - CRC_SIZE; // 483

// offsets ACK - 14
// format: [TIPO(1)] [SEQ(4)] [FRAG(4)] [STATUS(1)] [CRC(4)]
const int ACK_TYPE_OFF   = 0;
const int ACK_SEQ_OFF    = 1;
const int ACK_FRAG_OFF   = 5;
const int ACK_STATUS_OFF = 9;
const int ACK_CRC_OFF    = 10;

// for reconstruction
struct PendingMessage {
	int totalFragments = 0;
	int fragmentsReceived = 0;
	vector<string> fragments;
};

// hash
int calculateCRC(const char *buffer, int size);

// full msg
string buildMessage(int sequence, const string &data);
bool extractMessage(const string &msgStr, int &sequence, string &data);

// split & join msg
vector<string> fragmentMessage(const string &message);
string rebuildMessage(int sequence);

// for datagram
string buildDatagram(int sequence, int fragment, int totalFragments, const string &payload);
bool extractDatagram(const string &datagram, int &sequence, int &fragment, int &totalFragments, string &payload);

// for ACK
string buildACK(int sequence, int fragment, char status);
bool extractACK(const string &ackStr, int &sequence, int &fragment, char &status);

// for reception
bool isDuplicate(int sequence, int fragment);
void storeFragment(int sequence, int fragment, int totalFragments, const string &payload);
bool messageComplete(int sequence);
