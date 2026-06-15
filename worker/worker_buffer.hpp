#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>


// Reensambla los fragmentos de un mensaje (pesos/capas) recibidos por UDP
// y los entrega como un vector<float> listo para el motor de cómputo.
struct PendingMessage
{
    uint16_t totalFragments = 0;

    uint32_t receivedCount = 0;

    std::vector<std::string> fragments;

    std::vector<bool> received;
};


class WorkerBuffer
{
public:

    // Guarda un fragmento. Devuelve true si el mensaje 'sequence'
    // quedó completo con este fragmento.
    bool storeFragment(
        uint32_t sequence,
        uint16_t fragment,
        uint16_t totalFragments,
        const std::string& payload
    )
    {
        auto& msg = pending_[sequence];

        if(msg.fragments.empty())
        {
            msg.totalFragments = totalFragments;

            msg.fragments.resize(totalFragments);

            msg.received.resize(totalFragments, false);
        }

        // Fragmento duplicado (p.ej. el master reenvió porque
        // no le llegó nuestro ACK): lo ignoramos pero igual
        // confirmamos para que el master no siga reintentando.
        if(!msg.received[fragment])
        {
            msg.fragments[fragment] = payload;

            msg.received[fragment] = true;

            msg.receivedCount++;
        }

        return msg.receivedCount == msg.totalFragments;
    }


    // Concatena los fragmentos de 'sequence' y los reinterpreta
    // como un arreglo de floats (little-endian, igual que numpy/struct
    // de Python en x86).
    std::vector<float> extractWeights(
        uint32_t sequence
    )
    {
        auto& msg = pending_[sequence];

        std::string raw;

        raw.reserve(msg.fragments.size() * 483);

        for(const auto& frag : msg.fragments)
        {
            raw += frag;
        }

        size_t numFloats = raw.size() / sizeof(float);

        std::vector<float> weights(numFloats);

        std::memcpy(
            weights.data(),
            raw.data(),
            numFloats * sizeof(float)
        );

        return weights;
    }


    bool hasMessage(
        uint32_t sequence
    ) const
    {
        return pending_.find(sequence) != pending_.end();
    }


    void removeMessage(
        uint32_t sequence
    )
    {
        pending_.erase(sequence);
    }


private:

    std::map<uint32_t, PendingMessage> pending_;
};
