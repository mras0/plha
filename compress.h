#ifndef COMPRESS_H
#define COMPRESS_H

#include <vector>
#include <cstdint>
#include "lhaconsts.h"

std::vector<uint8_t> encode_lh(const std::vector<struct LzNode>& lz, LhaMethod method);

#endif
