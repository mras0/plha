#ifndef LHACOMPRESS_H
#define LHACOMPRESS_H

#include <vector>
#include <cstdint>
#include <string>
#include "lhaconsts.h"

void lha_compress(std::vector<uint8_t>& output, const std::vector<uint8_t>& uncompressed_data, const std::string& dirname, const std::string& filename, LhaMethod method, int64_t modtime);

#endif
