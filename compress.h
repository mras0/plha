#ifndef COMPRESS_H
#define COMPRESS_H

#include <vector>
#include <cstdint>

std::vector<uint8_t> encode_lh(const std::vector<struct LzNode>& lz, uint16_t window_bits);

#endif
