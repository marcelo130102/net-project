#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <bitset>
#include "protocol.hpp"

using namespace std;

// El historial almacena claves completas "IP:PORT:SEQ" para aislar ACKs de retransmisión por cliente
vector<string> recentlyCompleted;

string receiveMessageUDP(int sockfd, sockaddr_in &clientAddr, socklen_t &clientLen) {
    while (true) {
        // Ejecución preventiva del limpiador de memoria dinámica
        cleanupZombieMessages();

        char buffer[UDP_PACKET_SIZE];
        memset(buffer, 0, UDP_PACKET_SIZE);
        
        int n = recvfrom(sockfd, buffer, UDP_PACKET_SIZE, 0, (sockaddr*)&clientAddr, &clientLen);

        if (n != UDP_PACKET_SIZE)
            continue; 

        if (buffer[0] == TYPE_DATAGRAM) {
            int seq, frag, tot;
            string payload;
            string packet(buffer, UDP_PACKET_SIZE);

            // Intentar extraer el datagrama (Valida internamente el nuevo algoritmo CRC32)
            if (extractDatagram(packet, seq, frag, tot, payload)) {

                // Construcción de la llave unificada para soportar concurrencia masiva sin colisiones de secuencia
                string msgKey = to_string(clientAddr.sin_addr.s_addr) + ":" + 
                                to_string(clientAddr.sin_port) + ":" + bitset<32>(seq).to_string();

                cout << "[SERVER] Fragmento recibido de [" << inet_ntoa(clientAddr.sin_addr) 
                     << ":" << ntohs(clientAddr.sin_port) << "] -> Frg: " << (frag + 1) << " / " << tot << "\n";

                // Mitigación de desincronización aislando al esclavo específico
                if (find(recentlyCompleted.begin(), recentlyCompleted.end(), msgKey) != recentlyCompleted.end()) {
                    cout << "[SERVER] Paquete duplicado tardío de flujo ya cerrado. Reenviando ACK_COMPLETE de 500B...\n";
                    string finalAck = buildACK(seq, 0, ACK_COMPLETE);
                    sendto(sockfd, finalAck.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&clientAddr, clientLen);
                    continue;
                }

                // Guardar fragmento de forma segura en su mapa aislado
                if (!isDuplicate(msgKey, frag))
                    storeFragment(msgKey, frag, tot, payload);

                // Respuesta síncrona Stop-and-Wait
                string ack = buildACK(seq, frag, ACK_OK);
                sendto(sockfd, ack.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&clientAddr, clientLen);

                // Verificar si este esclavo en particular completó la transmisión de su matriz
                if (messageComplete(msgKey)) {
                    string fullMsg = rebuildMessage(msgKey);
                    
                    // Control estricto de fuga de memoria (Capa de historial circular con tope de 100 elementos)
                    recentlyCompleted.push_back(msgKey);
                    if (recentlyCompleted.size() > 100) {
                        recentlyCompleted.erase(recentlyCompleted.begin());
                    }

                    int msgSeq;
                    string data;

                    if (extractMessage(fullMsg, msgSeq, data)) {
                        // Cierre de flujo exitoso por socket individual
                        string finalAck = buildACK(seq, 0, ACK_COMPLETE);
                        sendto(sockfd, finalAck.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&clientAddr, clientLen);

                        return data;
                    }
                }
            }
            else {
                // --- PAQUETE CORRUPTO (FALLÓ CRC32) -> EMISIÓN DE NACK ---
                // Se extrae la cabecera forzando la conversión desde Network Byte Order
                int netBadSeq = 0, netBadFrag = 0;
                memcpy(&netBadSeq, &buffer[DG_SEQ_OFF], sizeof(int));
                memcpy(&netBadFrag, &buffer[DG_FRAG_OFF], sizeof(int));
                int badSeq = ntohl(netBadSeq);
                int badFrag = ntohl(netBadFrag);

                cout << "[SERVER] ¡CRC Real Inválido! Corrupción detectada en fragmento " << badFrag + 1 
                     << ". Emitiendo NACK inmediato de 500 Bytes...\n";
                
                string nack = buildACK(badSeq, badFrag, ACK_ERROR);
                sendto(sockfd, nack.data(), UDP_PACKET_SIZE, 0, (sockaddr*)&clientAddr, clientLen);
            }
        }
    }
}

int main() {
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    // --- NUEVO CÓDIGO: Aumentar el buffer UDP a ~8MB para soportar concurrencia ---
    int rcvBufferSize = 8 * 1024 * 1024; // 8 MB
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvBufferSize, sizeof(rcvBufferSize)) < 0) {
        cout << "[ALERTA] No se pudo aumentar el buffer de recepcion UDP.\n";
    }
    // ----------------------------------------------------------------------------
    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(45000);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    bind(sockfd, (sockaddr*)&serverAddr, sizeof(serverAddr));

    cout << "[SERVER] Capa de transporte activa y protegida. Escuchando en puerto 45000...\n";

    sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);

// El servidor procesará las solicitudes concurrentes aislando a los clientes limpiamente
    while(true) {
        string mensajeLimpio = receiveMessageUDP(sockfd, clientAddr, clientLen);
        
        cout << "\n--- MATRIZ DE PESOS EXTRAÍDA CON ÉXITO ---\n";
        cout << "Origen: [" << inet_ntoa(clientAddr.sin_addr) << ":" << ntohs(clientAddr.sin_port) << "]\n";
        cout << "Bytes recibidos: " << mensajeLimpio.size() << " bytes.\n";

        // ====================================================================
        // PRUEBA DE INTEGRIDAD: Transformar los bytes de vuelta a floats
        // ====================================================================
        int num_pesos_recibidos = mensajeLimpio.size() / sizeof(float);
        cout << "Cantidad de pesos (floats) reconstruidos: " << num_pesos_recibidos << "\n";

        if (num_pesos_recibidos >= 5) {
            const float* pesos = reinterpret_cast<const float*>(mensajeLimpio.data());
            
            cout << "Primeros 5 pesos recibidos para validación:\n";
            for(int i = 0; i < 5; i++) {
                cout << "Peso [" << i << "]: " << pesos[i] << "\n";
            }
            
            // Verificamos el último peso para asegurar que la cola del paquete llegó bien
            cout << "Último peso [" << num_pesos_recibidos - 1 << "]: " 
                 << pesos[num_pesos_recibidos - 1] << "\n";
        }
        cout << "-----------------------------------------------------\n";
    }

    close(sockfd);
    return 0;
}