#ifndef LHACONSTS_H
#define LHACONSTS_H

#include <cstdint>

static constexpr uint32_t max_match_len = 256;
static constexpr uint32_t min_match_len = 3;
static constexpr uint32_t NT = 16 + 3; // USHRT_BIT + 3
static constexpr uint32_t NC = 255 + max_match_len + 2 - min_match_len;
static constexpr uint32_t TBIT = 5;
static constexpr uint32_t CBIT = 9;

enum LhaMethod {
    LHA_METHOD_DIR, // directory entry
    LHA_METHOD_LH0, // uncompressed
    LHA_METHOD_LH5, // 8K dict
    LHA_METHOD_LH6, // 32K dict
    LHA_METHOD_LH7, // 64K dict
    LHA_METHOD_UNKNOWN
};

extern const uint8_t method_names[LHA_METHOD_UNKNOWN][5];

LhaMethod lha_method_from_id(const uint8_t (&method)[5]);

#endif
