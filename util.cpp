#include "util.h"
#include <print>
#include <cstdint>

void hexdump(const void* data, size_t size, size_t addr)
{
    auto dat = reinterpret_cast<const uint8_t*>(data);
    const size_t per_block = 16;
    for (size_t i = 0; i < size; i += per_block) {
        std::print("{:04X} ", i + addr);

        const auto here = std::min(size - i, per_block);
        for (size_t j = 0; j < here; ++j)
            std::print("{:02X} ", dat[i + j]);
        for (size_t j = here; j < per_block; ++j)
            std::print("   ");
        for (size_t j = 0; j < here; ++j) {
            const auto ch = dat[i + j];
            std::print("{:c}", ch >= ' ' && ch <= 0x7f ? ch : '.');
        }
        std::println("");
    }
}


