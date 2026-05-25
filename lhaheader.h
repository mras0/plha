#ifndef LHAHEADER_H
#define LHAHEADER_H

#include <cstdint>
#include <string>
#include <vector>

struct LhaHeader {
    size_t header_offset;
    size_t compressed_offset;

    uint8_t level;
    uint8_t compression_method[5];
    uint32_t compressed_size;
    uint32_t original_size;
    uint32_t protect;
    // In DOS date format
    uint16_t mod_time;
    uint16_t mod_date;
    std::string filename;
    std::string dirname;
    uint16_t crc;
    uint8_t os;
};

std::string lha_date_str(uint16_t t);
std::string lha_time_str(uint16_t t);

int64_t unix_time_from_dos_time(uint16_t date, uint16_t time);

void lha_header_set_dos_time(LhaHeader& hdr, int64_t t);
void lha_header_convert_unix_to_dos_time(LhaHeader& hdr);

#endif
