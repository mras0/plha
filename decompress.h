#ifndef DECOMPRESS_H
#define DECOMPRESS_H

#include <vector>
#include <cstdint>

std::vector<uint8_t> decompress(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size, const uint8_t (&method)[5]);
std::vector<uint8_t> decompress(const std::vector<uint8_t>& data, const struct LhaHeader& hdr);

#endif
