#include <print>
#include <string>
#include <stdexcept>
#include <vector>
#include <memory>
#include <cstdint>
#include <cassert>
#include <algorithm>

struct FileCloser {
    void operator()(FILE * fp)
    {
        if (fp)
            fclose(fp);
    }
};
using FilePointer = std::unique_ptr<FILE, FileCloser>;

FilePointer open_file(const std::string& filename, const char* mode)
{
    FilePointer fp {fopen(filename.c_str(), mode) };
    if (!fp)
        throw std::runtime_error { std::format("Could not open \"{}\" with mode {}", filename, mode) };
    return fp;
}

std::vector<std::uint8_t> read_file(const std::string& filename)
{
    auto fp = open_file(filename, "rb");
    fseek(fp.get(), 0, SEEK_END);
    size_t len = ftell(fp.get());
    std::vector<std::uint8_t> data(len);
    fseek(fp.get(), 0, SEEK_SET);
    if (len)
        fread(&data[0], 1, len, fp.get());
    return data;
}


void hexdump(const void* data, size_t size, size_t addr = 0)
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

// CRC-16-ANSI, x^16 + x^15 + x^2 +  1
uint16_t crc16(void* data, size_t size, uint16_t crc = 0)
{
    auto dat = reinterpret_cast<const uint8_t*>(data);
    #if 0
    while (size--) {
        crc ^= *dat++;
        for (int j = 0; j < 8; ++j) {
            if (crc & 1)
                crc = crc >> 1 ^ 0xA001;
            else
                crc = crc >> 1;
        }
    }
    #else
    static const uint16_t tab[] = {
        0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241,
        0xc601, 0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1, 0xc481, 0x0440,
        0xcc01, 0x0cc0, 0x0d80, 0xcd41, 0x0f00, 0xcfc1, 0xce81, 0x0e40,
        0x0a00, 0xcac1, 0xcb81, 0x0b40, 0xc901, 0x09c0, 0x0880, 0xc841,
        0xd801, 0x18c0, 0x1980, 0xd941, 0x1b00, 0xdbc1, 0xda81, 0x1a40,
        0x1e00, 0xdec1, 0xdf81, 0x1f40, 0xdd01, 0x1dc0, 0x1c80, 0xdc41,
        0x1400, 0xd4c1, 0xd581, 0x1540, 0xd701, 0x17c0, 0x1680, 0xd641,
        0xd201, 0x12c0, 0x1380, 0xd341, 0x1100, 0xd1c1, 0xd081, 0x1040,
        0xf001, 0x30c0, 0x3180, 0xf141, 0x3300, 0xf3c1, 0xf281, 0x3240,
        0x3600, 0xf6c1, 0xf781, 0x3740, 0xf501, 0x35c0, 0x3480, 0xf441,
        0x3c00, 0xfcc1, 0xfd81, 0x3d40, 0xff01, 0x3fc0, 0x3e80, 0xfe41,
        0xfa01, 0x3ac0, 0x3b80, 0xfb41, 0x3900, 0xf9c1, 0xf881, 0x3840,
        0x2800, 0xe8c1, 0xe981, 0x2940, 0xeb01, 0x2bc0, 0x2a80, 0xea41,
        0xee01, 0x2ec0, 0x2f80, 0xef41, 0x2d00, 0xedc1, 0xec81, 0x2c40,
        0xe401, 0x24c0, 0x2580, 0xe541, 0x2700, 0xe7c1, 0xe681, 0x2640,
        0x2200, 0xe2c1, 0xe381, 0x2340, 0xe101, 0x21c0, 0x2080, 0xe041,
        0xa001, 0x60c0, 0x6180, 0xa141, 0x6300, 0xa3c1, 0xa281, 0x6240,
        0x6600, 0xa6c1, 0xa781, 0x6740, 0xa501, 0x65c0, 0x6480, 0xa441,
        0x6c00, 0xacc1, 0xad81, 0x6d40, 0xaf01, 0x6fc0, 0x6e80, 0xae41,
        0xaa01, 0x6ac0, 0x6b80, 0xab41, 0x6900, 0xa9c1, 0xa881, 0x6840,
        0x7800, 0xb8c1, 0xb981, 0x7940, 0xbb01, 0x7bc0, 0x7a80, 0xba41,
        0xbe01, 0x7ec0, 0x7f80, 0xbf41, 0x7d00, 0xbdc1, 0xbc81, 0x7c40,
        0xb401, 0x74c0, 0x7580, 0xb541, 0x7700, 0xb7c1, 0xb681, 0x7640,
        0x7200, 0xb2c1, 0xb381, 0x7340, 0xb101, 0x71c0, 0x7080, 0xb041,
        0x5000, 0x90c1, 0x9181, 0x5140, 0x9301, 0x53c0, 0x5280, 0x9241,
        0x9601, 0x56c0, 0x5780, 0x9741, 0x5500, 0x95c1, 0x9481, 0x5440,
        0x9c01, 0x5cc0, 0x5d80, 0x9d41, 0x5f00, 0x9fc1, 0x9e81, 0x5e40,
        0x5a00, 0x9ac1, 0x9b81, 0x5b40, 0x9901, 0x59c0, 0x5880, 0x9841,
        0x8801, 0x48c0, 0x4980, 0x8941, 0x4b00, 0x8bc1, 0x8a81, 0x4a40,
        0x4e00, 0x8ec1, 0x8f81, 0x4f40, 0x8d01, 0x4dc0, 0x4c80, 0x8c41,
        0x4400, 0x84c1, 0x8581, 0x4540, 0x8701, 0x47c0, 0x4680, 0x8641,
        0x8201, 0x42c0, 0x4380, 0x8341, 0x4100, 0x81c1, 0x8081, 0x4040
    };
    while (size--) {
        crc = tab[(crc ^ *dat++) & 0xff] ^ (crc >> 8);
    }
#endif

    return crc;
}

struct LhaHeader {
    size_t header_offset;
    size_t compressed_offset;

    uint8_t level;
    uint8_t compression_method[5];
    uint32_t compressed_size;
    uint32_t original_size;
    uint32_t mod_time;
    std::string filename;
    uint16_t crc;
    uint8_t os;
};

class LhaFile {
public:
    explicit LhaFile(std::vector<uint8_t>&& data)
        : data_ { std::move(data) }
    {
    }

    bool next(LhaHeader& hdr)
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
        hdr.mod_time = get_u32(); // Interpretation depends on version
        skip(1); // Reserved (file attribute)
        skip(1); // Header level
        switch (hdr.level) {
        case 0:
            read_level0_header(hdr);
            break;
        case 1:
            read_level1_header(hdr);
            break;
        default:
            throw std::runtime_error { std::format("Header level {:d} not supported", hdr.level) };
        }
        skip(hdr.compressed_size);
        return true;
    }

private:
    std::vector<uint8_t> data_;
    size_t pos_;

    size_t remaining() const
    {
        return data_.size() - pos_;
    }

    void check_remaining(size_t count)
    {
        if (remaining() < count)
            throw std::runtime_error { "Out of data" };
    }

    void skip(size_t count)
    {
        check_remaining(count);
        pos_ += count;
    }

    uint8_t get_u8()
    {
        check_remaining(1);
        return data_[pos_++];
    }

    uint16_t get_u16()
    {
        uint16_t res = get_u8();
        res |= get_u8() << 8;
        return res;
    }

    uint32_t get_u32()
    {
        uint32_t res = get_u16();
        res |= get_u16() << 16;
        return res;
    }

    void get_raw(uint8_t* dest, size_t size)
    {
        check_remaining(size);
        memcpy(dest, &data_[pos_], size);
        pos_ += size;
    }

    std::string get_string(size_t size)
    {
        check_remaining(size);
        std::string s;
        s.assign(&data_[pos_], &data_[pos_ + size]);
        pos_ += size;
        return s;
    }

    void check_header_checksum(const LhaHeader& hdr)    
    {
        uint8_t hdr_size = data_[hdr.header_offset];
        uint8_t hdr_csum = data_[hdr.header_offset + 1];
        uint8_t csum = 0;
        for (int i = 0; i < hdr_size; ++i)
            csum += data_[hdr.header_offset + 2 + i];
        if (csum != hdr_csum)
            throw std::runtime_error { std::format("Invalid level {} header checksum: {:02X} != {:02X}", hdr.level, csum, hdr_csum) };
    }

    void read_level0_header(LhaHeader& hdr)
    {
        check_header_checksum(hdr);
        hdr.filename = get_string(get_u8());
        std::replace(hdr.filename.begin(), hdr.filename.end(), '\\', '/');
        hdr.crc = get_u16();

        const int ext_hdr_size = (int)(pos_ - hdr.header_offset) - (data_[hdr.header_offset] + 2);
        if (ext_hdr_size < 0)
            throw std::runtime_error { std::format("Invalid header size. Extended header size {}", ext_hdr_size) };
        if (ext_hdr_size) {
            hexdump(&data_[pos_], ext_hdr_size);
            throw std::runtime_error { "TODO! extended header in level0" };
        }
    }

    void read_level1_header(LhaHeader& hdr)
    {
        check_header_checksum(hdr);

        // TODO: "compressed_size" includes extended headers
        hdr.filename = get_string(get_u8());
        hdr.crc = get_u16();
        hdr.os = get_u8();

        for (uint16_t ext_size; (ext_size = get_u16()) != 0;) {
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
            case 0x02: {
                auto dirname = get_string(ext_size);
                std::replace(dirname.begin(), dirname.end(), '\xFF', '/');
                hdr.filename.insert(hdr.filename.begin(), dirname.begin(), dirname.end());
                break;
            }
            default:
                std::println("TODO: Extended header 0x{:02X} size 0x{:X}", type, ext_size);
                hexdump(&data_[pos_], ext_size);
                skip(ext_size);
            }
        }


        hdr.compressed_offset = pos_;
    }
};

int main()
{
    try {
        //const char* filename = R"(c:\Users\micha\Downloads\DylanDog_Complete_WHD.lha)"; // lh0/lh5
        //const char* filename = R"(c:\Users\micha\Downloads\WHDLoad_dev.lha)";// lh0/lh5
        //const char* filename = R"(c:\Temp\whdload_test\Kefrens-AnkhInPopland\source\Install\Kefrens-AnkhInPopland.lha)";
        //const char* filename = R"(c:\Users\micha\Downloads\tg93mods.lha)"; // Header level 0
        const char* filename = R"(c:\Users\micha\Downloads\3dstars.lha)"; // Header level 0 with directory
        auto data = read_file(filename);
        LhaFile lha { std::move(data) };
        for (LhaHeader hdr; lha.next(hdr);) {
            // Note: The filename can be NUL-terminated and contain version info afterwards
            std::println("{:5.5s} {:6d} {:6d} {}", (const char*)hdr.compression_method, hdr.compressed_size, hdr.original_size, hdr.filename);
        }

        //auto f = read_file(R"(c:\Temp\whdload_test\Kefrens-AnkhInPopland\source\Install\AnkhInPopland Install\source\explode.s)");
        //std::println("\nCRC: 0x{:4X}", crc16(f.data(), f.size()));
        //std::println("");

    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
