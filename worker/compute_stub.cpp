#include <cstdint>
#include <iostream>
#include <vector>

#include "compute_stub.hpp"


// Stub temporal
// Falta DNN

void processWeights(
    const std::vector<float>& weights,
    uint32_t sequence
)
{
    std::cout
        << "[Compute][stub] seq=" << sequence
        << " -> recibidos " << weights.size()
        << " floats."
        << std::endl;

}
