#pragma once
#include <iostream>
#include <cstdint>
#include <string>
#include <algorithm>



constexpr int DATAGRAM_SIZE = 500;

constexpr int TYPE_OFFSET = 0;

constexpr int SEQ_OFFSET = 1;

constexpr int FRAG_OFFSET = 5;

constexpr int TOTAL_FRAGS_OFFSET = 7;

constexpr int SIZE_OFFSET = 9;

constexpr int PAYLOAD_OFFSET = 13;

constexpr int CRC_OFFSET = 496;

constexpr int PAYLOAD_SIZE = 483;

constexpr char PADDING_CHAR = '#';


// ACK: Type(1) + Seq(4) + Frag(2) + Status(1) + CRC32(4) = 12 bytes
constexpr int ACK_TYPE_OFFSET = 0;

constexpr int ACK_SEQ_OFFSET = 1;

constexpr int ACK_FRAG_OFFSET = 5;

constexpr int ACK_STATUS_OFFSET = 7;

constexpr int ACK_CRC_OFFSET = 8;

constexpr int ACK_SIZE = 12;

constexpr uint8_t ACK_STATUS_OK = 1;

constexpr uint8_t ACK_STATUS_NACK = 0;


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

    uint8_t status;

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



inline std::string buildPaddedPayload(const std::string& data)
{
    std::string padded(PAYLOAD_SIZE, PADDING_CHAR);
    const size_t copySize = std::min(data.size(), static_cast<size_t>(PAYLOAD_SIZE));
    padded.replace(0, copySize, data.substr(0, copySize));
    return padded;
}

inline std::string extractPayload(const char* buffer, uint32_t dataSize)
{
    if (dataSize > static_cast<uint32_t>(PAYLOAD_SIZE))
    {
        dataSize = PAYLOAD_SIZE;
    }

    return std::string(buffer + PAYLOAD_OFFSET, dataSize);
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

    buffer += buildPaddedPayload(payload);

    return calculateCRC32(buffer);
}




uint32_t calculateAckCRC(
    char type,
    uint32_t sequence,
    uint16_t fragment,
    uint8_t status
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
        reinterpret_cast<char*>(&status),
        sizeof(status)
    );

    return calculateCRC32(buffer);
}