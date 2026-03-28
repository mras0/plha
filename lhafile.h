#ifndef LHAFILE_H
#define LHAFILE_H

#include "lhaheader.h"

class LhaFileReader {
public:
    explicit LhaFileReader(const uint8_t* data, size_t size);

    bool next(LhaHeader& hdr);
private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;

    size_t remaining() const;
    void check_remaining(size_t count);
    void skip(size_t count);
    uint8_t get_u8();
    uint16_t get_u16();
    uint32_t get_u32();
    void get_raw(uint8_t* dest, size_t size);
    std::string get_string(size_t size);

    void check_header_checksum(const LhaHeader& hdr);

    void read_level0_header(LhaHeader& hdr);
    void read_level1_header(LhaHeader& hdr);
    void read_level2_header(LhaHeader& hdr);

    void handle_extended_headers(LhaHeader& hdr);
};

bool is_method(const uint8_t (&l)[5], const uint8_t (&r)[5]);

#endif
