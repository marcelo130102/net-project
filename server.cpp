#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <vector>
#include <cstring>
#include <algorithm>
#include <arpa/inet.h>
#include <unistd.h>

//wa
#include "protocol.hpp"

using namespace std;

constexpr int PORT = 9000;




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

struct MessageAssembly
{
    uint16_t totalFragments;

    vector<string> fragments;
};

mutex clientsMutex;
mutex fragmentsMutex;

map<uint32_t, MessageAssembly> pendingMessages;



string buildClientKey(sockaddr_in addr) {

    return string(inet_ntoa(addr.sin_addr))
           + ":"
           + to_string(ntohs(addr.sin_port));
}











//ENVIO DE ACK-------------------
void sendACK(
    int sockfd,
    sockaddr_in addr,
    uint32_t sequence,
    uint16_t fragment,
    bool success
)
{
    char datagram[ACK_SIZE];

    memset(datagram,0,ACK_SIZE);


    //tipo
    datagram[ACK_TYPE_OFFSET] = 'A';


    //secuencia(se mantiene en todos los fragmentos solo para un mensaje)
    memcpy(
        datagram + ACK_SEQ_OFFSET,
        &sequence,
        sizeof(sequence)
    );

    //fragmento(1/5 ....)
    memcpy(
        datagram + ACK_FRAG_OFFSET,
        &fragment,
        sizeof(fragment)
    );

    //estado para hacer el doble ack = nack
    uint8_t status =
        success ? ACK_STATUS_OK : ACK_STATUS_NACK;

    memcpy(
        datagram + ACK_STATUS_OFFSET,
        &status,
        sizeof(status)
    );

    //calculamos el crc para el ack
    uint32_t crc =
        calculateAckCRC(
            'A',
            sequence,
            fragment,
            status
        );

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











void storeFragment(
    const DatagramInfo& packet
)
{
    auto& msg =
        pendingMessages[
            packet.sequence
        ];

    if(msg.fragments.empty())
    {
        msg.totalFragments =
            packet.totalFragments;

        msg.fragments.resize(
            packet.totalFragments
        );
    }


    msg.fragments[
        packet.fragment
    ] = packet.payload;
}



bool isComplete(
    const DatagramInfo& packet
)
{
    auto& msg =
        pendingMessages[
            packet.sequence
        ];

    for(const auto& frag : msg.fragments)
    {
        if(frag.empty())
        {
            return false;
        }
    }

    return true;
}



string rebuildMessage(
    uint32_t sequence
)
{
    string result;

    auto& msg =
        pendingMessages[
            sequence
        ];

    for(const auto& frag :
        msg.fragments)
    {
        result += frag;
    }

    return result;
}

void removeMessage(
    uint32_t sequence
)
{
    pendingMessages.erase(
        sequence
    );
}



bool receiveDatagram(
    int sockfd,
    char buffer[],
    sockaddr_in& clientAddr
) {

    socklen_t len =
        sizeof(clientAddr);

    int received =
        recvfrom(
            sockfd,
            buffer,
            DATAGRAM_SIZE,
            0,
            (sockaddr*)&clientAddr,
            &len
        );

    return received > 0;
}




//-----------wa-------------------------
DatagramInfo extractDatagramInfo(
    const char buffer[]
)
{
    DatagramInfo info;

    memcpy(
        &info.type,
        buffer + TYPE_OFFSET,
        1
    );

    memcpy(
        &info.sequence,
        buffer + SEQ_OFFSET,
        4
    );

    memcpy(
        &info.fragment,
        buffer + FRAG_OFFSET,
        2
    );

    memcpy(
        &info.totalFragments,
        buffer + TOTAL_FRAGS_OFFSET,
        2
    );

    memcpy(
        &info.dataSize,
        buffer + SIZE_OFFSET,
        4
    );

    info.payload = extractPayload(
        buffer,
        info.dataSize
    );

    memcpy(
        &info.crc,
        buffer + CRC_OFFSET,
        4
    );

    return info;
}



//
bool isDuplicateFragment(
    const DatagramInfo& packet
)
{
    auto it =
        pendingMessages.find(
            packet.sequence
        );

    if(it == pendingMessages.end())
    {
        return false;
    }

    auto& msg = it->second;

    if(
        packet.fragment >=
        msg.fragments.size()
    )
    {
        return false;
    }

    return
        !msg.fragments[
            packet.fragment
        ].empty();
}
































//-----------wa-------------------------












int createServerSocket() {

    int sockfd =
        socket(AF_INET, SOCK_DGRAM, 0);

    sockaddr_in serverAddr{};

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    ::bind(
        sockfd,
        (sockaddr*)&serverAddr,
        sizeof(serverAddr)
    );

    return sockfd;
}









int main()
{
    bool ackDropped = false;
    int sockfd =
        createServerSocket();

    cout
        << "UDP CHAT SERVER RUNNING\n";





    while(true)
    {
        char buffer[DATAGRAM_SIZE];

        sockaddr_in clientAddr{};

        if(
            !receiveDatagram(
                sockfd,
                buffer,
                clientAddr
            )
        )
        {
            continue;
        }

        DatagramInfo packet =
            extractDatagramInfo(
                buffer
            );

        //calcular crc
        uint32_t calculatedCRC =
            calculatePacketCRC(
                packet.type,
                packet.sequence,
                packet.fragment,
                packet.totalFragments,
                packet.dataSize,
                packet.payload
            );




        if(calculatedCRC != packet.crc)
        {
            cout << "CRC ERROR" << endl;

            sendACK(
                sockfd,
                clientAddr,
                packet.sequence,
                packet.fragment,
                false
            );

            continue;
        }
        else{
            cout << "\n\n\n>>CRC OK\n\n";

        }

        cout << "\n===== FRAGMENT RECEIVED =====\n";
        cout
            << "Received CRC: "
            << packet.crc
            << endl;

        cout
            << "Calculated CRC: "
            << calculatedCRC
            << endl;

        cout << "SEQ: "
             << packet.sequence
             << endl;

        cout << "FRAG: "
             << packet.fragment
             << "/"
             << packet.totalFragments - 1
             << endl;

        cout << "SIZE: "
             << packet.dataSize
             << endl;
        //VERIFICACION MENSAJES DUPLICADOSSSSSSSS
        if(
            isDuplicateFragment(
                packet
            )
        )
        {
            cout
                << "DUPLICATE FRAGMENT "
                << packet.fragment
                << endl;

            sendACK(
                sockfd,
                clientAddr,
                packet.sequence,
                packet.fragment,
                true
            );

            continue;
        }

        storeFragment(packet);

        auto& msg =
            pendingMessages[
                packet.sequence
            ];
        //-..............................................


        int count = 0;


        
        for(const auto& f : msg.fragments)
        {
            if(!f.empty())
                count++;
        }

        cout
            << "Stored "
            << count
            << "/"
            << msg.totalFragments
            << endl;           




        //PRUEBA-------------------------------
        static bool ackDropped = false;

        if(
            packet.fragment == 1
            &&
            !ackDropped
        )
        {
            cout
                << "ACK intentionally dropped"
                << endl;

            ackDropped = true;
        }
        else
        {
            sendACK(
                sockfd,
                clientAddr,
                packet.sequence,
                packet.fragment,
                true
            );
        }
        //----------------------------------



        if(
            isComplete(packet)
        )
        {
            string fullMessage =
                rebuildMessage(
                    packet.sequence
                );

            cout
                << "\n===== COMPLETE MESSAGE =====\n";

            cout
                << "MESSAGE SIZE: "
                << fullMessage.size()
                << endl;

            cout
                << fullMessage
                << endl;

            cout
                << "============================\n\n\n";

            removeMessage(
                packet.sequence
            );
        }
    }

    close(sockfd);

    return 0;
}
