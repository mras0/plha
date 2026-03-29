#include "lhaconsts.h"
#include <cstring>
#include <stdexcept>
#include <format>

const uint8_t lha_method_names[LHA_METHOD_UNKNOWN][6] = {
    { '-', 'l', 'h', 'd', '-', 0 },
    { '-', 'l', 'h', '0', '-', 0 },
    { '-', 'l', 'h', '5', '-', 0 },
    { '-', 'l', 'h', '6', '-', 0 },
    { '-', 'l', 'h', '7', '-', 0 },
};

LhaMethod lha_method_from_id(const uint8_t (&method)[5])
{
    for (int i = 0; i < LHA_METHOD_UNKNOWN; ++i)
        if (!std::memcmp(method, lha_method_names[i], 5))
            return (LhaMethod)i;
    return LHA_METHOD_UNKNOWN;
}

uint16_t window_bits_for_method(LhaMethod method)
{
    switch (method) {
    case LHA_METHOD_LH5:
        return 13;
    case LHA_METHOD_LH6:
        return 15;
    case LHA_METHOD_LH7:
        return 16;
    default:
        throw std::runtime_error { std::format("Invalid compression method {:5.5s}", (const char*)lha_method_names[method]) };
    }
}