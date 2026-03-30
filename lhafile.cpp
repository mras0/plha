#include "lhafile.h"
#include "util.h"
#include <stdexcept>
#include <print>
#include <algorithm>
#include <cstring>
#include <cassert>

LhaFileReader::LhaFileReader(const uint8_t* data, size_t size)
    : data_ { data }
    , size_ { size }
    , pos_ { 0 }
{
}

bool LhaFileReader::next(LhaHeader& hdr)
{
    if (remaining() < 22)
        return false;
    hdr = LhaHeader {};
    hdr.header_offset = pos_;
    hdr.level = data_[pos_ + 20];
    skip(2);
    get_raw(hdr.compression_method, sizeof(hdr.compression_method));
    hdr.compressed_size = get_u32(); // skip size for level 1!
    hdr.original_size = get_u32();
    // Interpretation depends on version
    hdr.mod_time = get_u16();
    hdr.mod_date = get_u16();
    skip(1); // Reserved (file attribute)
    skip(1); // Header level
    switch (hdr.level) {
    case 0:
        if (data_[hdr.header_offset] == 0 && data_[hdr.header_offset + 1] == 0) {
            std::println("Warning: trailing 0's (?) with {} bytes remain", remaining());
            return false;
        }
        read_level0_header(hdr);
        break;
    case 1:
        read_level1_header(hdr);
        break;
    case 2:
        read_level2_header(hdr);
        lha_header_convert_unix_to_dos_time(hdr);
        break;
    default:
        throw std::runtime_error { std::format("Header level {:d} not supported", hdr.level) };
    }
    hdr.compressed_offset = pos_;
    skip(hdr.compressed_size);
    return true;
}

size_t LhaFileReader::remaining() const
{
    return size_ - pos_;
}

void LhaFileReader::check_remaining(size_t count)
{
    if (remaining() < count)
        throw std::runtime_error { "Out of data" };
}

void LhaFileReader::skip(size_t count)
{
    check_remaining(count);
    pos_ += count;
}

uint8_t LhaFileReader::get_u8()
{
    check_remaining(1);
    return data_[pos_++];
}

uint16_t LhaFileReader::get_u16()
{
    uint16_t res = get_u8();
    res |= get_u8() << 8;
    return res;
}

uint32_t LhaFileReader::get_u32()
{
    uint32_t res = get_u16();
    res |= get_u16() << 16;
    return res;
}

void LhaFileReader::get_raw(uint8_t* dest, size_t size)
{
    check_remaining(size);
    std::memcpy(dest, &data_[pos_], size);
    pos_ += size;
}

std::string LhaFileReader::get_string(size_t size)
{
    check_remaining(size);
    std::string s;
    s.assign(&data_[pos_], &data_[pos_ + size]);
    pos_ += size;
    return s;
}

void LhaFileReader::check_header_checksum(const LhaHeader& hdr)
{
    uint8_t hdr_size = data_[hdr.header_offset];
    uint8_t hdr_csum = data_[hdr.header_offset + 1];
    uint8_t csum = 0;
    for (int i = 0; i < hdr_size; ++i)
        csum += data_[hdr.header_offset + 2 + i];
    if (csum != hdr_csum)
        throw std::runtime_error { std::format("Invalid level {} header checksum: {:02X} != {:02X}", hdr.level, csum, hdr_csum) };
}

void LhaFileReader::read_level0_header(LhaHeader& hdr)
{
    check_header_checksum(hdr);
    hdr.filename = get_string(get_u8());
    std::replace(hdr.filename.begin(), hdr.filename.end(), '\\', '/');
    if (const auto sep = hdr.filename.find_last_of('/'); sep != std::string::npos) {
        hdr.dirname = hdr.filename.substr(0, sep + 1);
        hdr.filename = hdr.filename.substr(sep + 1);
    }
    hdr.crc = get_u16();

    const int ext_hdr_size = (int)(pos_ - hdr.header_offset) - (data_[hdr.header_offset] + 2);
    if (ext_hdr_size < 0)
        throw std::runtime_error { std::format("Invalid header size. Extended header size {}", ext_hdr_size) };
    if (ext_hdr_size) {
        std::println("Possible extended header in level0"); // Most likely just garbage
        hexdump(&data_[pos_], ext_hdr_size);
        skip(ext_hdr_size);
        //throw std::runtime_error { "TODO! extended header in level0" };
    }
}

void LhaFileReader::read_level1_header(LhaHeader& hdr)
{
    check_header_checksum(hdr);

    // Note: "compressed_size" includes extended headers
    hdr.filename = get_string(get_u8());
    hdr.crc = get_u16();
    hdr.os = get_u8();
    handle_extended_headers(hdr);
}

void LhaFileReader::read_level2_header(LhaHeader& hdr)
{
    const uint16_t hdr_size = data_[hdr.header_offset] | data_[hdr.header_offset + 1];
    hdr.crc = get_u16();
    hdr.os = get_u8();
    handle_extended_headers(hdr);
    if (pos_ != hdr.header_offset + hdr_size)
        throw std::runtime_error { std::format("Invalid level 2 header size") };
}

void LhaFileReader::handle_extended_headers(LhaHeader& hdr)
{
    for (uint16_t ext_size; (ext_size = get_u16()) != 0;) {
        if (hdr.level == 1)
            hdr.compressed_size -= ext_size;
        ext_size -= 2;
        check_remaining(ext_size);
        if (!ext_size)
            throw std::runtime_error { "Empty extended header" };
        const uint8_t type = get_u8();
        --ext_size;
        switch (type) {
        case 0x00: // Common header. Has CRC16, but what is it over exactly?
            skip(ext_size);
            break;
        case 0x01:
            hdr.filename = get_string(ext_size);
            break;
        case 0x02:
            hdr.dirname = get_string(ext_size);
            std::replace(hdr.dirname.begin(), hdr.dirname.end(), '\xFF', '/');
            break;
        case 0x50: // Permissions (size = 2)
        case 0x51: // GID/UID (size = 4)
        case 0x54: // UNIX timestamp (size = 4)
            skip(ext_size);
            break;
        default:
            std::println("TODO: Extended header 0x{:02X} size 0x{:X}", type, ext_size);
            hexdump(&data_[pos_], ext_size);
            skip(ext_size);
        }
    }
}

bool is_method(const uint8_t (&l)[5], const uint8_t (&r)[5])
{
    return !memcmp(l, r, sizeof(l));
}

void lha_header_append(std::vector<uint8_t>& data, const LhaHeader& hdr)
{
    if (hdr.level != 1)
        throw std::runtime_error { std::format("Level {} header not supported in lha_header_append", hdr.level) };

    if (hdr.filename.size() > 255 - 27)
        throw std::runtime_error { std::format("Filename too long (should be in extended header)") };

    uint32_t ext_size = 0;
    if (!hdr.dirname.empty())
        ext_size += 3 + (uint32_t)hdr.dirname.size();

    auto put_u8 = [&data](uint8_t val) {
        data.push_back(val);
    };
    auto put_u16 = [&data](uint16_t val) {
        data.push_back((uint8_t)val);
        data.push_back((uint8_t)(val >> 8));
    };
    auto put_u32 = [&data](uint32_t val) {
        data.push_back((uint8_t)val);
        data.push_back((uint8_t)(val >> 8));
        data.push_back((uint8_t)(val >> 16));
        data.push_back((uint8_t)(val >> 24));
    };

    const size_t hdr_start = data.size();
    put_u8(0); // Hdr size (filled later)
    put_u8(0); // Hdr checksum (filled later)
    data.insert(data.end(), std::begin(hdr.compression_method), std::end(hdr.compression_method));
    put_u32(hdr.compressed_size + (hdr.level == 1 ? ext_size : 0));
    put_u32(hdr.original_size);
    // N.B. different for level 2 headers
    put_u16(hdr.mod_time);
    put_u16(hdr.mod_date);
    put_u8(0x20); // attributes (?!)
    put_u8(hdr.level);
    put_u8((uint8_t)hdr.filename.size());
    data.insert(data.end(), hdr.filename.begin(), hdr.filename.end());
    put_u16(hdr.crc);
    put_u8(hdr.os);
    const size_t hdr_end = data.size() + 2; // The first extended header size is included
    if (!hdr.dirname.empty()) {
        put_u16((uint16_t)(3 + hdr.dirname.size()));
        put_u8(2);
        auto dn = hdr.dirname;
        assert(dn.back() == '/');
        std::replace(dn.begin(), dn.end(), '\\', '\xFF');
        std::replace(dn.begin(), dn.end(), '/', '\xFF');
        data.insert(data.end(), dn.begin(), dn.end());
    }
    put_u16(0); // End of extended header
    // Update size
    data[hdr_start] = (uint8_t)(hdr_end - hdr_start - 2);
    // And checksum
    for (size_t i = hdr_start + 2; i < hdr_end; ++i)
        data[hdr_start + 1] += data[i];
}