#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <algorithm>
#include <cstring>
#include <arpa/inet.h>
#include <unistd.h>

//protocolos
#include "protocol.hpp"

using namespace std;
constexpr int PORT = 9000;
uint32_t nextSequence = 1;





struct DatagramInfo
{
    char type;

    uint32_t sequence;

    uint16_t fragment;

    uint16_t totalFragments;

    uint32_t dataSize;

    string payload;

    uint32_t crc;
};



mutex pendingFragmentsMutex;
//CLIENTES














void sendDataPacket(
    int sockfd,
    sockaddr_in serverAddr,
    uint32_t sequence,
    uint16_t fragment,
    uint16_t totalFragments,
    const string& data
)
{
    char datagram[DATAGRAM_SIZE];

    memset(datagram, 0, DATAGRAM_SIZE);

    datagram[TYPE_OFFSET] = 'D';

    uint32_t size = data.size();

    memcpy(
        datagram + SEQ_OFFSET,
        &sequence,
        sizeof(sequence)
    );

    memcpy(
        datagram + FRAG_OFFSET,
        &fragment,
        sizeof(fragment)
    );




    memcpy(
        datagram + TOTAL_FRAGS_OFFSET,
        &totalFragments,
        sizeof(totalFragments)
    );

    memcpy(
        datagram + SIZE_OFFSET,
        &size,
        sizeof(size)
    );

    memcpy(
        datagram + PAYLOAD_OFFSET,
        data.data(),
        size
    );


    uint32_t crc =
        calculatePacketCRC(
            'D',
            sequence,
            fragment,
            totalFragments,
            size,
            data
        );


    memcpy(
        datagram + CRC_OFFSET,
        &crc,
        sizeof(crc)
    );


    sendto(
        sockfd,
        datagram,
        DATAGRAM_SIZE,
        0,
        (sockaddr*)&serverAddr,
        sizeof(serverAddr)
    );
}

bool waitForACK(
    int sockfd,
    uint32_t expectedSequence,
    uint16_t expectedFragment
)
{
    char buffer[ACK_SIZE];
    uint32_t receivedCRC;


    sockaddr_in senderAddr{};

    socklen_t len =
        sizeof(senderAddr);

    int received =
        recvfrom(
            sockfd,
            buffer,
            ACK_SIZE,
            0,
            (sockaddr*)&senderAddr,
            &len
        );

    if(received <= 0)
    {
        cout << "TIMEOUT\n";
        return false;
    }

    if(buffer[0] != 'A')
    {
        return false;
    }

    uint32_t seq;

    memcpy(
        &seq,
        buffer + ACK_SEQ_OFFSET,
        sizeof(seq)
    );

    uint16_t fragment;

    memcpy(
        &fragment,
        buffer + ACK_FRAG_OFFSET,
        sizeof(fragment)
    );
    //CRC DE ACK
    memcpy(
        &receivedCRC,
        buffer + ACK_CRC_OFFSET,
        sizeof(receivedCRC)
    );

    uint32_t calculatedCRC =
        calculateAckCRC(
            seq,
            fragment
        );



    //VALIDAR CRC DE ACK
    if(receivedCRC != calculatedCRC)
    {
        cout
            << "ACK CRC ERROR"
            << endl;

        return false;
    }




    cout
        << "ACK received"
        << " | SEQ=" << seq
        << " | FRAG=" << fragment
        << " | CRC=" << receivedCRC
        << endl;

return
    seq == expectedSequence
    &&
    fragment == expectedFragment;
}









vector<string> fragmentMessage(
    const string &data
) {

    vector<string> chunks;

    int offset = 0;

    while (offset < data.size()) {

        int size =
            min(
                PAYLOAD_SIZE,
                (int)data.size() - offset
            );

        chunks.push_back(
            data.substr(offset, size)
        );

        offset += size;
    }

    return chunks;
}



void sendMessage(
    int sockfd,
    sockaddr_in serverAddr,
    const string& data
)
{
    vector<string> fragments =
        fragmentMessage(data);

    uint16_t total =
        fragments.size();

    cout
        << "Total fragments: "
        << total
        << endl;

    //Seq diferentes--------------------
    uint32_t messageSequence =
    nextSequence++; 


    for(uint16_t i=0; i<total; i++)
    {
        cout
            << "Sending fragment "
            << i
            << "/"
            << total-1
            << " size="
            << fragments[i].size()
            << endl;

    bool acknowledged = false;

    const int MAX_RETRIES = 5;

    int retries = 0;

    while(!acknowledged &&
      retries < MAX_RETRIES)
    {
        sendDataPacket(
            sockfd,
            serverAddr,
            messageSequence,
            i,
            total,
            fragments[i]
        );

        acknowledged =
            waitForACK(
                sockfd,
                messageSequence,
                i
            );

        if(!acknowledged)
        {
            retries++;

            cout
                << "Timeout. Retry "
                << retries
                << "/"
                << MAX_RETRIES
                << endl;
        }
    }
    
    if(!acknowledged)
    {
        cout
        << "Transmission failed"
        << endl;
    }

    }
}
























int createClientSocket() {

    int sockfd =
        socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        throw runtime_error(
            "Socket creation failed."
        );
    }

    sockaddr_in clientAddr{};

    clientAddr.sin_family = AF_INET;
    clientAddr.sin_addr.s_addr = INADDR_ANY;
    clientAddr.sin_port = htons(0);

    if (bind(
            sockfd,
            (sockaddr *)&clientAddr,
            sizeof(clientAddr)
        ) < 0) {

        close(sockfd);

        throw runtime_error(
            "Bind failed."
        );
    }

    return sockfd;
}

sockaddr_in createServerAddress() {

    sockaddr_in serverAddr{};

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);

    inet_pton(
        AF_INET,
        "127.0.0.1",
        &serverAddr.sin_addr
    );

    return serverAddr;
}




int main()
{
    int sockfd =
        createClientSocket();

    timeval timeout{};

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    setsockopt(
        sockfd,
        SOL_SOCKET,
        SO_RCVTIMEO,
        &timeout,
        sizeof(timeout)
    );


    sockaddr_in serverAddr =
        createServerAddress();
//PRUEBA VARIOS MENSAJES--------------------------
    sendMessage(
        sockfd,
        serverAddr,
        "Mensaje A"
    );

    sendMessage(
        sockfd,
        serverAddr,
        "Mensaje B"
    );

    sendMessage(
        sockfd,
        serverAddr,
        "Mensaje C"
    );

    close(sockfd);

    return 0;
}