#ifndef DECOMPRESS_H
#define DECOMPRESS_H

#include <vector>
#include <cstdint>
#include "lhaconsts.h"

std::vector<uint8_t> decompress(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size, LhaMethod method);
std::vector<uint8_t> decompress(const std::vector<uint8_t>& data, const struct LhaHeader& hdr);

#endif
