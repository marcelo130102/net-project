#include <cstring>
#include <algorithm>
#include <cmath>
#include <arpa/inet.h>
#include <chrono>
#include <iostream>
#include "protocol.hpp"

using namespace std;

// El mapa ahora utiliza un string compuesto "IP:PUERTO:SEQ" para aislar sockets de clientes simultáneos
map<string, PendingMessage> pendingMessages;

// Implementación estándar y robusta del algoritmo CRC32 (División polinómica)
int calculateCRC(const char *buffer, int size) {
    unsigned int crc = 0xFFFFFFFF;
    for (int i = 0; i < size; i++) {
        crc ^= static_cast<unsigned char>(buffer[i]);
        for (int j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ 0xEDB88320;
            else crc >>= 1;
        }
    }
    return static_cast<int>(~crc);
}

string buildMessage(int sequence, const string &data) {
    int dataSize = data.size();
    int totalSize = MSG_HEADER_SIZE + dataSize + CRC_SIZE;
    string output(totalSize, 0);

    output[MSG_TYPE_OFF] = TYPE_MESSAGE;
    
    // Convertir a Network Byte Order
    int netSequence = htonl(sequence);
    int netDataSize = htonl(dataSize);
    memcpy(&output[MSG_SEQ_OFF], &netSequence, sizeof(int));
    memcpy(&output[MSG_SIZE_OFF], &netDataSize, sizeof(int));

    memcpy(&output[MSG_HEADER_SIZE], data.data(), dataSize);

    int crc = calculateCRC(output.data(), MSG_HEADER_SIZE + dataSize);
    int netCrc = htonl(crc);
    memcpy(&output[MSG_HEADER_SIZE + dataSize], &netCrc, sizeof(int));

    return output;
}

bool extractMessage(const string &msgStr, int &sequence, string &data) {
    if(msgStr.empty() || msgStr[MSG_TYPE_OFF] != TYPE_MESSAGE)
        return false;

    int netSequence, netDataSize;
    memcpy(&netSequence, &msgStr[MSG_SEQ_OFF], sizeof(int));
    memcpy(&netDataSize, &msgStr[MSG_SIZE_OFF], sizeof(int));
    sequence = ntohl(netSequence);
    int dataSize = ntohl(netDataSize);

    int expectedTotalSize = MSG_HEADER_SIZE + dataSize + CRC_SIZE;
    if(msgStr.size() < expectedTotalSize)
        return false;

    int netReceivedCRC;
    memcpy(&netReceivedCRC, &msgStr[MSG_HEADER_SIZE + dataSize], sizeof(int));
    int receivedCRC = ntohl(netReceivedCRC);

    int calcCRC = calculateCRC(msgStr.data(), MSG_HEADER_SIZE + dataSize);
    if(receivedCRC != calcCRC)
        return false;

    data = msgStr.substr(MSG_HEADER_SIZE, dataSize);
    return true;
}

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

std::string rebuildMessage(const std::string &msgKey) {
    std::string completeMsg = "";
    if(pendingMessages.find(msgKey) != pendingMessages.end()) {
        
        // --- COMPLEMENTO OBLIGATORIO: Reservar memoria de golpe ---
        int totalExpectedSize = pendingMessages[msgKey].totalFragments * DG_MAX_PAYLOAD;
        completeMsg.reserve(totalExpectedSize);
        // -----------------------------------------------------------

        for(const std::string &frag : pendingMessages[msgKey].fragments) {
            completeMsg += frag;
        }
        pendingMessages.erase(msgKey); 
    }
    return completeMsg;
}

string buildDatagram(int sequence, int fragment, int totalFragments, const string &payload) {
    string output(UDP_PACKET_SIZE, 0);

    output[DG_TYPE_OFF] = TYPE_DATAGRAM;
    
    int netSeq = htonl(sequence);
    int netFrag = htonl(fragment);
    int netTot = htonl(totalFragments);
    memcpy(&output[DG_SEQ_OFF], &netSeq, sizeof(int));
    memcpy(&output[DG_FRAG_OFF], &netFrag, sizeof(int));
    memcpy(&output[DG_TOT_OFF], &netTot, sizeof(int));

    memcpy(&output[DG_DATA_OFF], payload.data(), payload.size());

    int crc = calculateCRC(output.data(), UDP_PACKET_SIZE - CRC_SIZE);
    int netCrc = htonl(crc);
    memcpy(&output[UDP_PACKET_SIZE - CRC_SIZE], &netCrc, sizeof(int));

    return output;
}

bool extractDatagram(const string &datagram, int &sequence, int &fragment, int &totalFragments, string &payload) {
    if(datagram.size() != UDP_PACKET_SIZE)
        return false;
    if(datagram[DG_TYPE_OFF] != TYPE_DATAGRAM)
        return false;

    int netReceivedCRC;
    memcpy(&netReceivedCRC, &datagram[UDP_PACKET_SIZE - CRC_SIZE], sizeof(int));
    int receivedCRC = ntohl(netReceivedCRC);
    
    int calcCRC = calculateCRC(datagram.data(), UDP_PACKET_SIZE - CRC_SIZE);
    if(receivedCRC != calcCRC)
        return false; 

    int netSeq, netFrag, netTot;
    memcpy(&netSeq, &datagram[DG_SEQ_OFF], sizeof(int));
    memcpy(&netFrag, &datagram[DG_FRAG_OFF], sizeof(int));
    memcpy(&netTot, &datagram[DG_TOT_OFF], sizeof(int));
    
    sequence = ntohl(netSeq);
    fragment = ntohl(netFrag);
    totalFragments = ntohl(netTot);

    payload = datagram.substr(DG_DATA_OFF, DG_MAX_PAYLOAD);
    return true;
}

string buildACK(int sequence, int fragment, char status) {
    string output(UDP_PACKET_SIZE, 0); 

    output[ACK_TYPE_OFF] = TYPE_ACK;
    int netSeq = htonl(sequence);
    int netFrag = htonl(fragment);
    memcpy(&output[ACK_SEQ_OFF], &netSeq, sizeof(int));
    memcpy(&output[ACK_FRAG_OFF], &netFrag, sizeof(int));
    output[ACK_STATUS_OFF] = status;

    int crc = calculateCRC(output.data(), UDP_PACKET_SIZE - CRC_SIZE);
    int netCrc = htonl(crc);
    memcpy(&output[ACK_CRC_OFF], &netCrc, sizeof(int));

    return output;
}

bool extractACK(const string &ackStr, int &sequence, int &fragment, char &status) {
    if (ackStr.size() != UDP_PACKET_SIZE)
        return false;
    if (ackStr[ACK_TYPE_OFF] != TYPE_ACK)
        return false;

    int netReceivedCRC;
    memcpy(&netReceivedCRC, &ackStr[ACK_CRC_OFF], sizeof(int));
    int receivedCRC = ntohl(netReceivedCRC);
    
    int calcCRC = calculateCRC(ackStr.data(), UDP_PACKET_SIZE - CRC_SIZE);
    if (receivedCRC != calcCRC)
        return false; 

    int netSeq, netFrag;
    memcpy(&netSeq, &ackStr[ACK_SEQ_OFF], sizeof(int));
    memcpy(&netFrag, &ackStr[ACK_FRAG_OFF], sizeof(int));
    
    sequence = ntohl(netSeq);
    fragment = ntohl(netFrag);
    status = ackStr[ACK_STATUS_OFF];

    return true;
}

bool isDuplicate(const string &msgKey, int fragment) {
    if(pendingMessages.find(msgKey) == pendingMessages.end())
        return false;
    if(fragment >= pendingMessages[msgKey].fragments.size())
        return false;

    return !pendingMessages[msgKey].fragments[fragment].empty();
}

void storeFragment(const string &msgKey, int fragment, int totalFragments, const string &payload) {
    if(pendingMessages.find(msgKey) == pendingMessages.end()) {
        pendingMessages[msgKey].totalFragments = totalFragments;
        pendingMessages[msgKey].fragments.resize(totalFragments, "");
        pendingMessages[msgKey].fragmentsReceived = 0;
    }

    // Registrar marca de tiempo activa para evitar la acumulación de zombies
    pendingMessages[msgKey].lastActivity = chrono::steady_clock::now();

    if(pendingMessages[msgKey].fragments[fragment].empty()) {
        pendingMessages[msgKey].fragments[fragment] = payload;
        pendingMessages[msgKey].fragmentsReceived++;
    }
}

bool messageComplete(const string &msgKey) {
    if(pendingMessages.find(msgKey) == pendingMessages.end())
        return false;
    return pendingMessages[msgKey].fragmentsReceived == pendingMessages[msgKey].totalFragments;
}

// Recolector de basura periódico para fragmentos huérfanos (Timeout de inactividad de 15 segundos)
void cleanupZombieMessages() {
    auto now = chrono::steady_clock::now();
    for (auto it = pendingMessages.begin(); it != pendingMessages.end(); ) {
        if (chrono::duration_cast<chrono::seconds>(now - it->second.lastActivity).count() > 15) {
            cout << "[SERVER CLEANUP] Liberando memoria de mensaje zombi. Clave: " << it->first << "\n";
            it = pendingMessages.erase(it);
        } else {
            ++it;
        }
    }
}

void updateRTT(RTTMetrics &metrics, double measuredRTT) {
    double alpha = 0.125;
    double beta = 0.25;

    double error = measuredRTT - metrics.estimatedRTT;
    metrics.estimatedRTT = metrics.estimatedRTT + alpha * error;
    metrics.devRTT = metrics.devRTT + beta * (std::abs(error) - metrics.devRTT);
    
    metrics.timeout = metrics.estimatedRTT + 4.0 * metrics.devRTT;

    // --- CÓDIGO ACTUALIZADO: Tope máximo alineado a 2.0 segundos (2000 ms) ---
    if (metrics.timeout < 0.1) metrics.timeout = 0.1; 
    if (metrics.timeout > 2.0) metrics.timeout = 2.0; 
    // --------------------------------------------------------------------------
}

void applyBackoff(RTTMetrics &metrics) {
    metrics.backoffCount++;
    metrics.timeout *= 2.0; 
}

void resetBackoff(RTTMetrics &metrics) {
    metrics.backoffCount = 0;
}