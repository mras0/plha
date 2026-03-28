#include "lhaconsts.h"
#include <cstring>

const uint8_t method_names[LHA_METHOD_UNKNOWN][5] = {
    { '-', 'l', 'h', 'd', '-' },
    { '-', 'l', 'h', '0', '-' },
    { '-', 'l', 'h', '5', '-' },
    { '-', 'l', 'h', '6', '-' },
    { '-', 'l', 'h', '7', '-' },
};

LhaMethod lha_method_from_id(const uint8_t (&method)[5])
{
    for (int i = 0; i < LHA_METHOD_UNKNOWN; ++i)
        if (!std::memcmp(method, method_names[i], 5))
            return (LhaMethod)i;
    return LHA_METHOD_UNKNOWN;
}
