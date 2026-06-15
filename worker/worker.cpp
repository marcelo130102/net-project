#include <arpa/inet.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

#include "compute_stub.hpp"
#include "protocol.hpp"
#include "worker_buffer.hpp"

using namespace std;


constexpr int DEFAULT_WORKER_PORT = 9100;


void sendAck(
    int sockfd,
    sockaddr_in addr,
    uint32_t sequence,
    uint16_t fragment,
    bool success
)
{
    char datagram[ACK_SIZE];

    memset(datagram, 0, ACK_SIZE);

    datagram[ACK_TYPE_OFFSET] = 'A';

    memcpy(
        datagram + ACK_SEQ_OFFSET,
        &sequence,
        sizeof(sequence)
    );

    memcpy(
        datagram + ACK_FRAG_OFFSET,
        &fragment,
        sizeof(fragment)
    );

    uint8_t status = success ? 1 : 0;

    memcpy(
        datagram + ACK_STATUS_OFFSET,
        &status,
        sizeof(status)
    );

    uint32_t crc = calculateAckCRC(sequence, fragment);

    memcpy(
        datagram + ACK_CRC_OFFSET,
        &crc,
        sizeof(crc)
    );

    sendto(
        sockfd,
        datagram,
        ACK_SIZE,
        0,
        (sockaddr*)&addr,
        sizeof(addr)
    );
}


int main(int argc, char** argv)
{
    int port = DEFAULT_WORKER_PORT;

    if(argc > 1)
    {
        port = atoi(argv[1]);
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if(sockfd < 0)
    {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if(bind(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0)
    {
        perror("bind");
        return 1;
    }

    cout << "[Worker] escuchando en puerto " << port << endl;

    WorkerBuffer buffer;

    char raw[DATAGRAM_SIZE];

    while(true)
    {
        sockaddr_in masterAddr{};

        socklen_t len = sizeof(masterAddr);

        int received =
            recvfrom(
                sockfd,
                raw,
                DATAGRAM_SIZE,
                0,
                (sockaddr*)&masterAddr,
                &len
            );

        if(received <= 0)
        {
            continue;
        }

        if(received != DATAGRAM_SIZE)
        {
            cout
                << "[Worker] datagrama de tamano inesperado ("
                << received << " bytes), descartado"
                << endl;

            continue;
        }

        if(static_cast<uint8_t>(raw[TYPE_OFFSET]) != DATA)
        {
            // Solo nos interesan paquetes DATA; ACK/NACK son
            // los que nosotros enviamos hacia el master.
            continue;
        }

        // ---- Deserialización directa: buffer crudo -> struct nativo ----
        const auto* header =
            reinterpret_cast<const DataPacketHeader*>(raw);

        uint32_t sequence       = header->sequence;
        uint16_t fragment       = header->fragment;
        uint16_t totalFragments = header->totalFragments;
        uint32_t dataSize       = header->dataSize;

        if(dataSize > PAYLOAD_SIZE)
        {
            cout
                << "[Worker] dataSize invalido (" << dataSize
                << ") seq=" << sequence
                << ", descartado"
                << endl;

            continue;
        }

        string payload(raw + PAYLOAD_OFFSET, dataSize);

        // ---- Verificación de integridad (CRC32) ----
        uint32_t receivedCRC;

        memcpy(&receivedCRC, raw + CRC_OFFSET, sizeof(receivedCRC));

        uint32_t calculatedCRC =
            calculatePacketCRC(
                'D',
                sequence,
                fragment,
                totalFragments,
                dataSize,
                payload
            );

        if(receivedCRC != calculatedCRC)
        {
            cout
                << "[Worker] CRC invalido seq=" << sequence
                << " frag=" << fragment
                << " -> NACK"
                << endl;

            sendAck(sockfd, masterAddr, sequence, fragment, false);

            continue;
        }

        // CRC ok -> confirmamos este fragmento
        sendAck(sockfd, masterAddr, sequence, fragment, true);

        bool complete =
            buffer.storeFragment(
                sequence,
                fragment,
                totalFragments,
                payload
            );

        cout
            << "[Worker] fragmento " << (fragment + 1)
            << "/" << totalFragments
            << " seq=" << sequence
            << " OK"
            << endl;

        if(complete)
        {
            vector<float> weights = buffer.extractWeights(sequence);

            cout
                << "[Worker] mensaje seq=" << sequence
                << " completo (" << weights.size() << " floats)"
                << endl;

            processWeights(weights, sequence);

            buffer.removeMessage(sequence);
        }
    }

    close(sockfd);

    return 0;
}
