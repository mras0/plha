#ifndef LZ_H
#define LZ_H

#include <cstdint>
#include <vector>

struct LzNode {
    uint16_t code;
    uint16_t ofs;
};

std::vector<LzNode> lz_build(const uint8_t* data, uint32_t size, uint32_t max_match, uint16_t window_bits, uint32_t max_matches = 64);

#endif
