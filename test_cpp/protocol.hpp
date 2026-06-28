#pragma once

#include <string>
#include <vector>
#include <map>
#include <chrono>

using namespace std;

// Configuración general
const int UDP_PACKET_SIZE = 500;
const int ACK_PACKET_SIZE = 500;
const int TIMEOUT_SECONDS = 1;

// Tipos de paquetes
const char TYPE_MESSAGE  = 'M';
const char TYPE_DATAGRAM = 'D';
const char TYPE_ACK      = 'A';

// Estados de ACK
const char ACK_ERROR     = 0; // NACK
const char ACK_OK        = 1;
const char ACK_COMPLETE  = 2; // Mensaje completo

// Offsets del mensaje (Capa superior)
const int MSG_TYPE_OFF    = 0;
const int MSG_SEQ_OFF     = 1;
const int MSG_SIZE_OFF    = 5;
const int MSG_HEADER_SIZE = 9;
const int CRC_SIZE        = 4;

// Offsets del datagrama (500 Bytes)
const int DG_TYPE_OFF    = 0;
const int DG_SEQ_OFF     = 1;
const int DG_FRAG_OFF    = 5;
const int DG_TOT_OFF     = 9;
const int DG_DATA_OFF    = 13;
const int DG_HEADER_SIZE = 13;
const int DG_MAX_PAYLOAD = UDP_PACKET_SIZE - DG_HEADER_SIZE - CRC_SIZE; // 483 B

// Offsets de ACK (500 Bytes con Padding)
const int ACK_TYPE_OFF   = 0;
const int ACK_SEQ_OFF    = 1;
const int ACK_FRAG_OFF   = 5;
const int ACK_STATUS_OFF = 9;
const int ACK_CRC_OFF    = UDP_PACKET_SIZE - CRC_SIZE;

// Estructura de reconstrucción con protección contra zombis
struct PendingMessage {
    int totalFragments = 0;
    int fragmentsReceived = 0;
    vector<string> fragments;
    chrono::steady_clock::time_point lastActivity; 
};

struct RTTMetrics {
    double estimatedRTT = 0.5; 
    double devRTT = 0.25;      
    double timeout = 1.0;      
    int backoffCount = 0;
};

// Cálculo de CRC32 Real
int calculateCRC(const char *buffer, int size);

// Gestión de Mensajes Completos
string buildMessage(int sequence, const string &data);
bool extractMessage(const string &msgStr, int &sequence, string &data);

// Fragmentación y Reconstrucción Multi-Cliente
vector<string> fragmentMessage(const string &message);
string rebuildMessage(const string &msgKey);

// Gestión de Datagramas
string buildDatagram(int sequence, int fragment, int totalFragments, const string &payload);
bool extractDatagram(const string &datagram, int &sequence, int &fragment, int &totalFragments, string &payload);

// Gestión de ACKs/NACKs
string buildACK(int sequence, int fragment, char status);
bool extractACK(const string &ackStr, int &sequence, int &fragment, char &status);

// Recepción e Integridad Binaria
bool isDuplicate(const string &msgKey, int fragment);
void storeFragment(const string &msgKey, int fragment, int totalFragments, const string &payload);
bool messageComplete(const string &msgKey);
void cleanupZombieMessages(); 

// Métricas Jacobson-Karels & Backoff
void updateRTT(RTTMetrics &metrics, double measuredRTT);
void applyBackoff(RTTMetrics &metrics);
void resetBackoff(RTTMetrics &metrics);