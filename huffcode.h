#ifndef HUFFCODE_H
#define HUFFCODE_H

#include <vector>
#include <cstdint>

std::vector<uint32_t> codelen_huff(const std::vector<uint32_t>& prob);
std::vector<uint32_t> codelen_packing_merge(const std::vector<uint32_t>& prob, uint32_t max_len);
std::vector<uint32_t> assign_codes(const std::vector<uint32_t>& codelen);

#endif
