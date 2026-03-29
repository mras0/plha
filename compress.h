#ifndef COMPRESS_H
#define COMPRESS_H

#include <vector>
#include <cstdint>
#include "lhaconsts.h"

std::vector<uint8_t> compress(const std::vector<struct LzNode>& lz, LhaMethod method);
std::vector<uint8_t> compress(const uint8_t* data, uint32_t size, LhaMethod method);

#endif
