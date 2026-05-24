#ifndef LHACOMPRESS_H
#define LHACOMPRESS_H

#include <vector>
#include <cstdint>
#include <string>
#include "lhaconsts.h"

struct LhaCompressOptions {
    LhaMethod method = LHA_METHOD_LH5;
    uint32_t max_ratio_percent = 97;
};

void lha_compress(std::vector<uint8_t>& output, const std::vector<uint8_t>& uncompressed_data, const std::string& dirname, const std::string& filename, int64_t modtime, const LhaCompressOptions& options);

#endif
