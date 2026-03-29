#ifndef LZ_H
#define LZ_H

#include <cstdint>
#include <vector>

//static constexpr uint16_t window_bits = 13; // LH5
//static constexpr uint32_t window_size = 1U << window_bits;
//static constexpr uint32_t window_mask = window_size - 1;

struct LzNode {
    uint16_t code;
    uint16_t ofs;
};

std::vector<LzNode> lz_build(const uint8_t* data, uint32_t size, uint16_t window_bits);

#endif
