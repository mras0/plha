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
    if (len) {
        if (auto l = fread(&data[0], 1, len, fp.get()); l != len)
            throw std::runtime_error { std::format("Error reading from \"{}\" {} <> {}", filename, l, len) };
    }
    return data;
}

void write_file(const std::string& filename, const void* data, size_t size)
{
    auto fp = open_file(filename, "wb");
    if (auto sz = fwrite(data, 1, size, fp.get()); sz != size)
        throw std::runtime_error { std::format("Error writing to \"{}\" {} <> {}", filename, sz, size) };
}

void write_file(const std::string& filename, const std::vector<uint8_t>& data)
{
    write_file(filename, data.data(), data.size());
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
    std::string dirname;
    uint16_t crc;
    uint8_t os;
};

class LhaFile {
public:
    explicit LhaFile(const uint8_t* data, size_t size)
        : data_ { data }
        , size_ { size }
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
        case 2:
            read_level2_header(hdr);
            break;
        default:
            throw std::runtime_error { std::format("Header level {:d} not supported", hdr.level) };
        }
        hdr.compressed_offset = pos_;
        skip(hdr.compressed_size);
        return true;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;

    size_t remaining() const
    {
        return size_ - pos_;
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
        if (const auto sep = hdr.filename.find_last_of('/'); sep != std::string::npos) {
            hdr.dirname = hdr.filename.substr(0, sep + 1);
            hdr.filename = hdr.filename.substr(sep + 1);
        }
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
        handle_extended_headers(hdr);
    }

    void read_level2_header(LhaHeader& hdr)
    {
        const uint16_t hdr_size = data_[hdr.header_offset] | data_[hdr.header_offset + 1];
        hdr.crc = get_u16();
        hdr.os = get_u8();
        handle_extended_headers(hdr);
        if (pos_ != hdr.header_offset + hdr_size)
            throw std::runtime_error { std::format("Invalid level 2 header size") };
    }

    void handle_extended_headers(LhaHeader& hdr)
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
};

class InputBitString {
public:
    explicit InputBitString(const uint8_t* data, uint32_t size)
        : data_ { data }
        , end_ { data + size }
    {
        fill();
    }

    void drop(uint32_t cnt)
    {
        assert(cnt <= bufcnt_);
        bufcnt_ -= cnt;
        bitbuf_ &= (1 << bufcnt_) - 1;
        fill();
    }

    uint16_t peek(uint32_t cnt)
    {
        assert(cnt <= bufcnt_);
        return static_cast<uint16_t>(bitbuf_ >> (bufcnt_ - cnt));
    }

    uint16_t get(uint32_t cnt)
    {
        auto res = peek(cnt);
        drop(cnt);
        return res;
    }

private:
    const uint8_t* data_;
    const uint8_t* const end_;
    uint32_t bitbuf_ = 0;
    uint32_t bufcnt_ = 0;

    void fill()
    {
        while (bufcnt_ < 16) {
            bitbuf_ <<= 8;
            if (data_ < end_)
                bitbuf_ |= *data_++;
            else if (++data_ == end_ + 3)
                throw std::runtime_error { "Input overrun" };
            bufcnt_ += 8;
        }
    }
};

//static constexpr uint32_t max_dict_bits = 13; // LH5
//static constexpr uint32_t max_dict_size = 1 << max_dict_bits;

static constexpr uint32_t max_match = 256;
static constexpr uint32_t treshold = 3;

//static constexpr uint32_t NP = max_dict_bits + 1;
static constexpr uint32_t NT = 16 + 3; // USHRT_BIT + 3
static constexpr uint32_t NC = 255 + max_match + 2 - treshold;

//static constexpr uint32_t PBIT = 4; // 5; // For LH4/LH5, 6 and 7 have PBIT = 5
static constexpr uint32_t TBIT = 5;
static constexpr uint32_t CBIT = 9;

//static constexpr uint32_t NPT = 128;

class HuffTable {
public:
    explicit HuffTable(uint16_t num_syms, uint32_t table_bits)
        : num_syms_ { num_syms }
        , table_bits_ { table_bits }
        , code_len_(num_syms)
    {
        assert(table_bits_ <= 16);
        assert(1 << table_bits_ <= max_table_size);
        table_.resize(size_t(1) << table_bits_);
    }

    void read_pt_len(InputBitString& ibs, uint32_t nbit, uint32_t special = UINT32_MAX)
    {
        const uint32_t n = ibs.get(nbit);
        if (!n) {
            empty_table(ibs.get(nbit));
            return;
        }

        uint32_t i = 0;
        while (i < (int)std::min(n, (uint32_t)num_syms_)) {
            // k=7 -> 1110  k=8 -> 11110  k=9 -> 111110 ...
            uint16_t c = ibs.get(3);
            if (c == 7) {
                while (ibs.get(1))
                    ++c;
            }
            code_len_[i++] = (uint8_t)c;
            if (i == special) {
                c = ibs.get(2);
                while (c-- && i < num_syms_)
                    code_len_[i++] = 0;
            }
        }
        while (i < num_syms_)
            code_len_[i++] = 0;

        make_table();
    }

    void read_c_len(InputBitString& ibs, const HuffTable& tab)
    {
        const uint32_t n = ibs.get(CBIT);
        if (!n) {
            empty_table(ibs.get(CBIT));
            return;
        }

        uint32_t i = 0;
        while (i < (int)std::min(n, (uint32_t)num_syms_)) {
            auto len = tab.decode(ibs);
            if (len <= 2) {
                if (len == 0)
                    len = 1;
                else if (len == 1)
                    len = ibs.get(4) + 3;
                else
                    len = ibs.get(CBIT) + 20;
                if (i + len > table_.size())
                    throw std::runtime_error { "Invalid table in c_len" };
                while (len--)
                    code_len_[i++] = 0;
            } else {
                code_len_[i++] = uint8_t(len - 2);
            }
        }
        while (i < num_syms_)
            code_len_[i++] = 0;

        make_table();
    }

    uint16_t decode(InputBitString& ibs) const
    {
        auto sym = table_[ibs.peek(table_bits_)];
        if (sym < num_syms_) {
            ibs.drop(code_len_[sym]);
        } else {
            ibs.drop(table_bits_);
            do {
                if (ibs.get(1))
                    sym = right_[sym];
                else
                    sym = left_[sym];
            } while (sym >= num_syms_);
        }
        return sym;
    }

private:
    static constexpr uint32_t max_table_size = 4096;
    static constexpr uint32_t tree_size = 2 * NC - 1;
    const uint16_t num_syms_;
    const uint32_t table_bits_;
    std::vector<uint8_t> code_len_;
    std::vector<uint16_t> table_;
    uint16_t left_[tree_size];
    uint16_t right_[tree_size];

    void make_table();

    void empty_table(uint16_t sym)
    {
        for (uint32_t i = 0; i < num_syms_; ++i)
            code_len_[i] = 0;
        for (auto& tc : table_)
            tc = sym;
        return;
    }
};

void HuffTable::make_table()
{
    static constexpr uint32_t max_sym_bits = 16;
    static constexpr bool debug = false;

    uint16_t count[max_sym_bits + 1] = { 0 };
    uint16_t weight[max_sym_bits + 1];
    uint16_t start[max_sym_bits + 1];
    
    for (uint32_t i = 1; i <= max_sym_bits; ++i)
        weight[i] = 1 << (max_sym_bits - i);

    for (uint32_t i = 0; i < num_syms_; ++i) {
        if (code_len_[i] > max_sym_bits)
            throw std::runtime_error { std::format("Invalid code length {} in make_table", code_len_[i]) };
        if constexpr (debug)
            std::println("{:03X} {}", i, code_len_[i]);
        count[code_len_[i]]++;
    }

    uint32_t total = 0;
    for (uint32_t i = 1; i <= max_sym_bits; ++i) {
        start[i] = (uint16_t)total;
        total += weight[i] * count[i];
        if constexpr (debug)
            std::println("{:2d} start={:04X} weight={:04X} count={} total={:04X}", i, start[i], weight[i], count[i], total);
    }
    if (total & 0xffff)
        throw std::runtime_error { std::format("Invalid total {:X} in make_table", total) };

    const uint16_t m = uint16_t(16 - table_bits_);

    for (uint32_t i = 1; i <= table_bits_; ++i) {
        start[i] >>= m;
        weight[i] >>= m;
    }

    const uint32_t table_size = std::min(1U << table_bits_, max_table_size);
    assert(table_size == table_.size());

    memset(left_, 0, sizeof(left_));
    memset(right_, 0, sizeof(right_));
    memset(&table_[0], 0, table_size * sizeof(table_[0]));
    uint16_t avail = num_syms_;

    for (uint32_t sym = 0; sym < num_syms_; ++sym) {
        const uint32_t clen = code_len_[sym];
        if (!clen)
            continue;
        const uint16_t first_code = start[clen];
        const uint16_t last_code = start[clen] + weight[clen];
        start[clen] = last_code;

        if constexpr (debug && false) {
            auto bs = [&]() {
                if (clen <= table_bits_)
                    return std::format("{:0{}b}", first_code >> (table_bits_ - clen), clen);
                else
                    return std::format("{:0{}b}", first_code >> (16 - clen), clen);
            };

            std::println("{:02X} {} {}", sym, clen, bs());
        }

        if (clen <= table_bits_) {
            assert(first_code < table_size);
            assert(last_code <= table_size);
            for (uint32_t i = first_code; i < last_code; ++i)
                table_[i] = (uint16_t)sym;
        } else {
            uint32_t code = first_code;
            if (code >> m > table_size)
                throw std::runtime_error { std::format("Invalid table entry first_code {:X}", first_code) };
            uint16_t* entry = &table_[code >> m];
            code <<= table_bits_;
            uint32_t tree_len = clen - table_bits_;
            while (tree_len--) {
                if (!*entry) {
                    left_[avail] = right_[avail] = 0;
                    *entry = avail++;
                }
                entry = (code & 0x8000 ? right_ : left_) + *entry;
                code <<= 1;
            }
            *entry = (uint16_t)sym;
        }
    }

    if constexpr (debug && false) {
        for (uint32_t i = 0; i < table_size; ++i)
            std::println("{:02X} {:0{}b} {:04X}", i, i, table_bits_, table_[i]);

        for (uint32_t i = num_syms_; i < avail; ++i)
            std::println("{:02X} left = {:02X} right = {:02X}", i, left_[i], right_[i]);
    }
}

class Decompressor {
public:
    static std::vector<uint8_t> decode(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size, uint16_t dict_bits)
    {
        return Decompressor { data, compressed_size, uncompressed_size, dict_bits }.do_decode();
    }
    
private:
    explicit Decompressor(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size, uint16_t dict_bits)
        : dict_bits_ { dict_bits }
        , ibs_ { data, compressed_size }
        , out_(uncompressed_size)
        , ctab_ { NC, 12 }
        , ptab_ { uint16_t(dict_bits + 1), 8 }
    {
    }

    const uint16_t dict_bits_;
    InputBitString ibs_;
    std::vector<uint8_t> out_;
    HuffTable ctab_;
    HuffTable ptab_;
    uint16_t blocksize_ = 0;

    std::vector<uint8_t> do_decode();
    uint16_t decode_char();
    uint32_t decode_pos();
};

uint16_t Decompressor::decode_char()
{
    if (!blocksize_) {
        blocksize_ = ibs_.get(16);
        HuffTable clen_table { NT, 8 };
        clen_table.read_pt_len(ibs_, TBIT, 3);
        ctab_.read_c_len(ibs_, clen_table);
        ptab_.read_pt_len(ibs_, dict_bits_ < 14 ? 4 : 5);
    }
    blocksize_--;

    return ctab_.decode(ibs_);
}

uint32_t Decompressor::decode_pos()
{
    uint16_t p = ptab_.decode(ibs_);
    return 1 + (p ? (1 << (p - 1)) | ibs_.get(p - 1) : 0);
}

std::vector<uint8_t> Decompressor::do_decode()
{
    for (uint32_t outpos = 0; outpos < out_.size();) {
        const auto sym = decode_char();
        if (sym < 256) {
            out_[outpos++] = (uint8_t)sym;
            continue;
        }

        uint32_t len = sym - (256 - treshold);
        const uint32_t pos = decode_pos();

        if (outpos + len > out_.size())
            throw std::runtime_error { "Match is too long!" };
        if (pos > outpos)
            throw std::runtime_error { std::format("Invalid match pos {} out pos {}", pos, outpos) };
        for (; len--; ++outpos)
            out_[outpos] = out_[outpos - pos];
    }
    return std::move(out_);
}


std::vector<uint8_t> decompress(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size, const uint8_t (&method)[5])
{
    if (!memcmp(method, "-lh0-", sizeof(method))) {
        assert(uncompressed_size == compressed_size);
        return std::vector<uint8_t>(data, data + uncompressed_size);
    }
    if (!memcmp(method, "-lh5-", sizeof(method)))
        return Decompressor::decode(data, compressed_size, uncompressed_size, 13); // 8K dict
    if (!memcmp(method, "-lh6-", sizeof(method)))
        return Decompressor::decode(data, compressed_size, uncompressed_size, 15); // 32K dict
    if (!memcmp(method, "-lh7-", sizeof(method)))
        return Decompressor::decode(data, compressed_size, uncompressed_size, 16); // 64K dict

    throw std::runtime_error { std::format("Unsupported compression method {:5.5s}", (const char*)method) };
}

std::vector<uint8_t> decompress(const std::vector<uint8_t>& data, const LhaHeader& hdr)
{
    auto res = decompress(&data[hdr.compressed_offset], hdr.compressed_size, hdr.original_size, hdr.compression_method);
    if (auto crc = crc16(res.data(), res.size()); crc != hdr.crc)
        throw std::runtime_error { std::format("CRC mismatch for {}. {:04x} <> {:04x}", hdr.filename.c_str(), crc, hdr.crc) };
    return res;
}

void test_file(const std::string& filename)
{
    auto data = read_file(filename);
    LhaFile lha { data.data(), data.size() };
    for (LhaHeader hdr; lha.next(hdr);) {
        // Note: The filename can be NUL-terminated and contain version info afterwards
        std::println("{:5.5s} {:6d} {:6d} {}{}", (const char*)hdr.compression_method, hdr.compressed_size, hdr.original_size, hdr.dirname, hdr.filename);
        if (!memcmp(hdr.compression_method, "-lhd-", 5))
            continue;
        (void)decompress(data, hdr);
    }
}

int main()
{
    try {
        const char* const tests[] = {
            R"(c:\Users\micha\Downloads\DylanDog_Complete_WHD.lha)", // lh0/lh5
            R"(c:\Users\micha\Downloads\WHDLoad_dev.lha)",// lh0/lh5
            R"(c:\Temp\whdload_test\Kefrens-AnkhInPopland\source\Install\Kefrens-AnkhInPopland.lha)",
            R"(c:\Users\micha\Downloads\tg93mods.lha)", // Header level 0
            R"(c:\Users\micha\Downloads\3dstars.lha)", // Header level 0 with directory
            R"(explode.lha)",
            R"(spaces.lha)",
            R"(c:\Users\micha\Downloads\SDK_54.16.lha)", // lh0/lh6
            R"(c:\Users\micha\Downloads\elk-knarkzilla.lha)", // Empty table in c_len     
            R"(c:\Users\micha\Downloads\gcc68.lha)", // lhz7/header level 2, match position > outpos -- TODO
        };

        for (const auto& fn : tests)
            test_file(fn);

    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
