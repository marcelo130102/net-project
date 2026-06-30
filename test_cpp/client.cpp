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

// Global flags for fault injection testing
bool test_loss = false;
bool test_corrupt = false;
bool test_timeout = false;

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

            bool enviarPaqueteNormal = true;
            
            // Inyectar pérdida
            if (test_loss && i == 1 && metrics.backoffCount == 0) {
                cout << "   >> [TEST PÉRDIDA] Tirando fragmento " << (i + 1) << " a la basura para forzar Timeout...\n";
                enviarPaqueteNormal = false; 
            }
            
            // Inyectar corrupción
            static bool corromperFragmento4 = true;
            if (test_corrupt && i == 3 && corromperFragmento4) {
                cout << "   >> [TEST CORRUPCIÓN] Alterando bits del fragmento " << (i + 1) << " para romper el CRC...\n";
                datagram[DG_DATA_OFF] ^= 0xFF;
                corromperFragmento4 = false;
            }

            // Registrar marca de tiempo inicial (Jacobson-Karels)
            auto timeStart = steady_clock::now();

            if (enviarPaqueteNormal) {
                sendto(sockfd, datagram.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&destAddr, destLen);
            }

            char buffer[UDP_PACKET_SIZE];
            memset(buffer, 0, UDP_PACKET_SIZE);
            
            int n = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, nullptr, nullptr);

            // Inyectar pérdida de ACK (timeout)
            static bool ignorarAckUnaVez = true;
            if (test_timeout && i == 5 && ignorarAckUnaVez && n > 0) {
                cout << "   >> [TEST ACK PERDIDO] Ignorando el ACK del fragmento " << (i + 1) << " para obligar a retransmitir un duplicado...\n";
                ignorarAckUnaVez = false;
                n = -1; // Forzamos un -1 para que salte al bloque 'else' de timeout
            }

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

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--test-loss") == 0) test_loss = true;
        if (strcmp(argv[i], "--test-corrupt") == 0) test_corrupt = true;
        if (strcmp(argv[i], "--test-timeout") == 0) test_timeout = true;
    }

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






