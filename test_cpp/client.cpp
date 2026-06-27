#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
#include "protocol.hpp"

using namespace std;
using namespace std::chrono;

// Instancia de métricas global para el flujo Cliente -> Servidor
RTTMetrics metrics;
const double BACKOFF_MAX_MS = 2.0; // Tope estricto de 2000 ms (2.0 segundos) solicitado por el equipo

// Modifica el temporizador nativo del socket usando el RTO dinámico actual
void applySocketTimeout(int sockfd, double timeoutSec) {
    struct timeval tv;
    tv.tv_sec = static_cast<long>(timeoutSec);
    tv.tv_usec = static_cast<long>((timeoutSec - tv.tv_sec) * 1000000);
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

bool sendMessageUDP(int sockfd, sockaddr_in &destAddr, int sequence, const string &data) {
    string msgStr = buildMessage(sequence, data);
    vector<string> fragments = fragmentMessage(msgStr);
    socklen_t destLen = sizeof(destAddr);

    cout << "[CLIENT] Enviando mensaje SEQ " << sequence << " en " << fragments.size() << " fragmentos...\n";

    resetBackoff(metrics);

    for (int i = 0; i < fragments.size(); i++) {
        bool ackReceived = false;

        while (!ackReceived) {
            // Ajustar timeout dinámico del socket antes de transmitir
            applySocketTimeout(sockfd, metrics.timeout);

            string datagram = buildDatagram(sequence, i, fragments.size(), fragments[i]);
            cout << "[CLIENT] Enviando fragmento " << (i + 1) << " / " << fragments.size() 
                 << " [Timeout actual: " << metrics.timeout * 1000.0 << " ms]...\n";

            // Registrar marca de tiempo inicial (Jacobson-Karels)
            auto timeStart = steady_clock::now();

            sendto(sockfd, datagram.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&destAddr, destLen);

            char buffer[UDP_PACKET_SIZE];
            memset(buffer, 0, UDP_PACKET_SIZE);
            
            int n = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, nullptr, nullptr);

            if (n == UDP_PACKET_SIZE && buffer[0] == TYPE_ACK) {
                int ackSeq, ackFrag; 
                char ackStatus;

                if (extractACK(string(buffer, UDP_PACKET_SIZE), ackSeq, ackFrag, ackStatus)) {
                    if (ackSeq == sequence && ackFrag == i) {
                        if (ackStatus == ACK_OK || ackStatus == ACK_COMPLETE) {
                            // --- ACK EXITOSO ---
                            auto timeEnd = steady_clock::now();
                            double measuredRTT = duration_cast<duration<double>>(timeEnd - timeStart).count();
                            
                            // Actualizar fórmulas de red e interrupción de retransmisión
                            updateRTT(metrics, measuredRTT);
                            resetBackoff(metrics);
                            ackReceived = true; 
                            cout << "[CLIENT] ACK recibido para fragmento " << (i + 1) << "\n";
                        } 
                        else if (ackStatus == ACK_ERROR) {
                            // --- NACK DETECTADO por error de CRC en el Servidor ---
                            cout << "[CLIENT] <<NACK>> recibido por corrupcion en servidor. Retransmitiendo inmediatamente...\n";
                        }
                    }
                }
            }
            else {
                // --- TIMEOUT DETECTADO ---
                if (n < 0) {
                    cout << "[CLIENT] Timeout expirado. Aplicando Backoff Exponencial...\n";
                    applyBackoff(metrics);
                    if (metrics.timeout > BACKOFF_MAX_MS) {
                        metrics.timeout = BACKOFF_MAX_MS; // Acotar estrictamente al tope de 2s
                    }
                }
            }
        }
    }

    // Flujo de cierre: Asegurar la recepción del ACK de mensaje completo (Capa de orquestación)
    cout << "[CLIENT] Todos los fragmentos enviados. Esperando ACK_COMPLETE...\n";
    bool completeAckReceived = false;

    while (!completeAckReceived) {
        applySocketTimeout(sockfd, metrics.timeout);

        char buffer[UDP_PACKET_SIZE];
        memset(buffer, 0, UDP_PACKET_SIZE);
        int n = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, nullptr, nullptr);

        if (n == UDP_PACKET_SIZE && buffer[0] == TYPE_ACK) {
            int ackSeq, ackFrag; char ackStatus;
            if (extractACK(string(buffer, UDP_PACKET_SIZE), ackSeq, ackFrag, ackStatus)) {
                if (ackSeq == sequence && ackStatus == ACK_COMPLETE) {
                    completeAckReceived = true;
                    cout << "[CLIENT] Sincronizacion exitosa: ACK_COMPLETE recibido.\n";
                }
            }
        } else {
            cout << "[CLIENT] Timeout esperando ACK_COMPLETE, retransmitiendo ultimo fragmento...\n";
            string lastDatagram = buildDatagram(sequence, fragments.size() - 1, fragments.size(), fragments.back());
            sendto(sockfd, lastDatagram.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&destAddr, destLen);
        }
    }

    return true;
}

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(45000);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    int num_pesos = 82800; // ~331 KB de payload real
    int num_epocas = 3;

    for (int epoch = 1; epoch <= num_epocas; epoch++) {
        cout << "\n[CLIENT] --- SIMULANDO ENTRENAMIENTO: ÉPOCA " << epoch << " ---\n";
        
        // Simular que la IA está "entrenando" (tomando tiempo de CPU)
        std::this_thread::sleep_for(std::chrono::seconds(2)); 

        vector<float> matrizSimulada(num_pesos);
        for (int i = 0; i < num_pesos; i++) {
            matrizSimulada[i] = epoch + (0.001f * i); // Los datos cambian en cada época
        }

        string payloadBinary(reinterpret_cast<const char*>(matrizSimulada.data()), matrizSimulada.size() * sizeof(float));
        
        // ¡Usamos 'epoch' como el Sequence ID de tu protocolo!
        sendMessageUDP(sockfd, serverAddr, epoch, payloadBinary);
    }

    close(sockfd);
    return 0;
}








/*
pruebas de fallas en el envio 

            // =================================================================
            // INYECTOR DE ERRORES ACADÉMICO (Casos de Prueba - Persona A)
            // =================================================================
            bool enviarPaqueteNormal = true;

            // CASO 1: Simular Pérdida Física (Provoca TIMEOUT y BACKOFF)
            // Forzamos la pérdida del Fragmento 2 en su primer intento (índice 1)
            if (i == 1 && metrics.backoffCount == 0) {
                cout << "   >> [TEST PÉRDIDA] Tirando fragmento " << (i + 1) << " a la basura para forzar Timeout...\n";
                enviarPaqueteNormal = false; 
            }

            // CASO 2: Simular Corrupción de Datos (Provoca NACK en el Servidor)
            // Alteramos un byte del payload del Fragmento 4 en su primer intento (índice 3)
            static bool corromperFragmento4 = true;
            if (i == 3 && corromperFragmento4) {
                cout << "   >> [TEST CORRUPCIÓN] Alterando bits del fragmento " << (i + 1) << " para romper el CRC...\n";
                datagram[DG_DATA_OFF] ^= 0xFF; // Invertimos bits para romper el CRC
                corromperFragmento4 = false;   // ¡Desactivar para el siguiente intento!
            }

            // CASO 3: Simular Pérdida de ACK (Provoca DUPLICADOS en el Servidor)
            // El servidor recibe el Fragmento 6 (índice 5), pero el cliente ignora el ACK una vez
            static bool ignorarAckUnaVez = true;

            // Registrar marca de tiempo inicial (Jacobson-Karels)
            auto timeStart = steady_clock::now();

            // Solo enviamos si el CASO 1 no lo bloqueó
            if (enviarPaqueteNormal) {
                sendto(sockfd, datagram.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&destAddr, destLen);
            }

            char buffer[UDP_PACKET_SIZE];
            memset(buffer, 0, UDP_PACKET_SIZE);
            
            int n = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, nullptr, nullptr);

            // Inyección del CASO 3: Fingimos un timeout tirando el ACK real recibido
            if (i == 5 && ignorarAckUnaVez && n > 0) {
                cout << "   >> [TEST ACK PERDIDO] Ignorando el ACK del fragmento " << (i + 1) << " para obligar a retransmitir un duplicado...\n";
                ignorarAckUnaVez = false;
                n = -1; // Forzamos un -1 para que salte al bloque 'else' de timeout
            }
            // =================================================================*/