#include "nd/core/UUID.h"
#include <random>

namespace nd {

static std::mt19937_64& Engine() {
    static std::random_device rd;
    static std::mt19937_64 eng(((uint64_t)rd() << 32) ^ rd());
    return eng;
}

UUID::UUID() {
    std::uniform_int_distribution<uint64_t> dist(1, UINT64_MAX);
    m_Value = dist(Engine());
}

} // namespace nd
