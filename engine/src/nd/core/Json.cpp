#include "nd/core/Json.h"

namespace nd {

static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    for (size_t i = 0; i < size; i += 3) {
        uint32_t n = (uint32_t)p[i] << 16;
        if (i + 1 < size) n |= (uint32_t)p[i + 1] << 8;
        if (i + 2 < size) n |= p[i + 2];
        out.push_back(kB64[(n >> 18) & 63]);
        out.push_back(kB64[(n >> 12) & 63]);
        out.push_back(i + 1 < size ? kB64[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < size ? kB64[n & 63] : '=');
    }
    return out;
}

std::vector<uint8_t> Base64Decode(const std::string& text) {
    int table[256];
    for (int& t : table) t = -1;
    for (int i = 0; i < 64; i++) table[(unsigned char)kB64[i]] = i;
    std::vector<uint8_t> out;
    out.reserve(text.size() * 3 / 4);
    uint32_t buf = 0;
    int bits = 0;
    for (unsigned char c : text) {
        if (table[c] < 0) continue;
        buf = (buf << 6) | (uint32_t)table[c];
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((buf >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace nd
