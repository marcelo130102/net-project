#pragma once
#include <iostream>
#include <cstdint>
#include <string>



constexpr int DATAGRAM_SIZE = 500;

constexpr int TYPE_OFFSET = 0;

constexpr int SEQ_OFFSET = 1;

constexpr int FRAG_OFFSET = 5;

constexpr int TOTAL_FRAGS_OFFSET = 7;

constexpr int SIZE_OFFSET = 9;

constexpr int PAYLOAD_OFFSET = 13;

constexpr int CRC_OFFSET = 496;

constexpr int PAYLOAD_SIZE = 483;


//ACK
constexpr int ACK_TYPE_OFFSET = 0;

constexpr int ACK_SEQ_OFFSET = 1;

constexpr int ACK_FRAG_OFFSET = 5;

constexpr int ACK_CRC_OFFSET = 7;

constexpr int ACK_SIZE = 11;


enum PacketType : uint8_t
{
    DATA = 'D',
    ACK  = 'A',
    NACK = 'N'
};




struct DataPacket
{
    uint8_t  type;           // D

    uint32_t sequence;       // seq

    uint16_t fragment;       // frag

    uint16_t totalFragments; // total

    uint32_t dataSize;       // bytes válidos

    char payload[483];

    uint32_t crc;
};




struct Ack
{
    char type;

    uint32_t sequence;

    uint16_t fragment;

    uint32_t crc;
};


struct NackPacket
{
    uint8_t type;

    uint32_t sequence;

    uint32_t crc;
};



uint32_t calculateCRC32(
    const std::string& data
)
{
    uint32_t crc = 0xFFFFFFFF;

    for(unsigned char byte : data)
    {
        crc ^= byte;

        for(int i = 0; i < 8; i++)
        {
            if(crc & 1)
            {
                crc =
                    (crc >> 1)
                    ^
                    0xEDB88320;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}



uint32_t calculatePacketCRC(
    char type,
    uint32_t sequence,
    uint16_t fragment,
    uint16_t totalFragments,
    uint32_t dataSize,
    const std::string& payload
)
{
    std::string buffer;

    buffer.append(&type, 1);

    buffer.append(
        reinterpret_cast<char*>(&sequence),
        sizeof(sequence)
    );

    buffer.append(
        reinterpret_cast<char*>(&fragment),
        sizeof(fragment)
    );

    buffer.append(
        reinterpret_cast<char*>(&totalFragments),
        sizeof(totalFragments)
    );

    buffer.append(
        reinterpret_cast<char*>(&dataSize),
        sizeof(dataSize)
    );

    buffer += payload;

    return calculateCRC32(buffer);
}




uint32_t calculateAckCRC(
    uint32_t sequence,
    uint16_t fragment
)
{
    std::string buffer;

    buffer.append(
        reinterpret_cast<char*>(&sequence),
        sizeof(sequence)
    );

    buffer.append(
        reinterpret_cast<char*>(&fragment),
        sizeof(fragment)
    );

    return calculateCRC32(buffer);
}