#include "decompress.h"
#include "ibs.h"
#include "lhaconsts.h"
#include "crc16.h"
#include "lhaheader.h"
#include <cassert>
#include <stdexcept>
#include <cstring>
#include <print>

class HuffTable {
public:
    explicit HuffTable(uint16_t num_syms, uint32_t table_bits)
        : num_syms_ { num_syms }
        , table_bits_ { table_bits }
        , code_len_(num_syms)
    {
        assert(table_bits_ <= 16);
        assert(1U << table_bits_ <= max_table_size);
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
        while (i < std::min(n, (uint32_t)num_syms_)) {
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
        while (i < std::min(n, (uint32_t)num_syms_)) {
            auto len = tab.decode(ibs);
            if (len <= 2) {
                if (len == 0)
                    len = 1;
                else if (len == 1)
                    len = ibs.get(4) + 3;
                else
                    len = ibs.get(CBIT) + 20;
                if (i + len > num_syms_)
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
    static constexpr bool debug = false;

    uint16_t count[max_code_bits + 1] = { 0 };
    uint16_t weight[max_code_bits + 1];
    uint16_t start[max_code_bits + 1];
    
    for (uint32_t i = 1; i <= max_code_bits; ++i)
        weight[i] = 1 << (max_code_bits - i);

    for (uint32_t i = 0; i < num_syms_; ++i) {
        if (code_len_[i] > max_code_bits)
            throw std::runtime_error { std::format("Invalid code length {} in make_table", code_len_[i]) };
        if constexpr (debug)
            std::println("{:03X} {}", i, code_len_[i]);
        count[code_len_[i]]++;
    }

    uint32_t total = 0;
    for (uint32_t i = 1; i <= max_code_bits; ++i) {
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

    std::memset(left_, 0, sizeof(left_));
    std::memset(right_, 0, sizeof(right_));
    std::memset(&table_[0], 0, table_size * sizeof(table_[0]));
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
#if 0
    const uint32_t dict_mask = (1 << dict_bits_) - 1;
    std::vector<uint8_t> dict(size_t(1) << dict_bits_, ' ');
    uint32_t dictpos = 0;

    const uint32_t decoded_size = (uint32_t)out_.size();
    out_.clear(); // XXX....

    for (uint32_t decoded = 0; decoded < decoded_size;) {
        const auto sym = decode_char();
        if (sym < 256) {
            dict[dictpos] = (uint8_t)sym;
            if (dictpos++ == dict_mask) {
                out_.insert(out_.end(), &dict[0], &dict[0] + dictpos);
                dictpos = 0;
            }
            ++decoded;
            continue;
        }

        const uint32_t len = sym - (256 - treshold);
        const uint32_t off = decode_pos();
        const uint32_t pos = (dictpos - off) & dict_mask;
        decoded += len;

        for (uint32_t i = 0; i < len; ++i) {
            uint8_t c = dict[(pos + i) & dict_mask];
            dict[dictpos] = c;
            if (dictpos++ == dict_mask) {
                out_.insert(out_.end(), &dict[0], &dict[0] + dictpos);
                dictpos = 0;
            }
        }
    }
    out_.insert(out_.end(), &dict[0], &dict[0] + dictpos);
    assert(out_.size() == decoded_size);

#else
    for (uint32_t outpos = 0; outpos < out_.size();) {
        const auto sym = decode_char();
        if (sym < 256) {
            out_[outpos++] = (uint8_t)sym;
            continue;
        }

        uint32_t len = (sym - 256) + min_match_len;
        uint32_t pos = decode_pos();

        if (outpos + len > out_.size())
            throw std::runtime_error { "Match is too long!" };
        while (pos > outpos && len) {
            // The dictionary is initially filled with spaces...
            out_[outpos++] = ' ';
            len--;
        }
        for (; len--; ++outpos)
            out_[outpos] = out_[outpos - pos];
    }
#endif
    return std::move(out_);
}


std::vector<uint8_t> decompress(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size, LhaMethod method)
{
    if (method == LHA_METHOD_LH0) {
        assert(uncompressed_size == compressed_size);
        return std::vector<uint8_t>(data, data + uncompressed_size);
    }

    return Decompressor::decode(data, compressed_size, uncompressed_size, window_bits_for_method(method));
}

std::vector<uint8_t> decompress(const std::vector<uint8_t>& data, const LhaHeader& hdr)
{
    const auto method = lha_method_from_id(hdr.compression_method);
    if (method == LHA_METHOD_UNKNOWN)
        throw std::runtime_error { std::format("Unsupported compression method {:5.5s}", (const char*)hdr.compression_method) };

    auto res = decompress(&data[hdr.compressed_offset], hdr.compressed_size, hdr.original_size, method);
    if (auto crc = crc16(res.data(), res.size()); crc != hdr.crc)
        throw std::runtime_error { std::format("CRC mismatch for {}. {:04x} <> {:04x}", hdr.filename.c_str(), crc, hdr.crc) };
    return res;
}

