#ifndef LHAHEADER_H
#define LHAHEADER_H

#include <cstdint>
#include <string>

struct LhaHeader {
    size_t header_offset;
    size_t compressed_offset;

    uint8_t level;
    uint8_t compression_method[5];
    uint32_t compressed_size;
    uint32_t original_size;
    uint32_t mod_time;
    std::string filename;
    std::string dirname;
    uint16_t crc;
    uint8_t os;
};

#endif
