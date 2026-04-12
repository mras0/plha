#include "decompress.h"
#include "ibs.h"
#include "lhaconsts.h"
#include "crc16.h"
#include "lhaheader.h"
#include "hufftable.h"
#include "dynhuff.h"
#include <cassert>
#include <stdexcept>
#include <print>

template<typename Self>
class DecompressorBase {
public:
    static std::vector<uint8_t> decode(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size, uint16_t dict_bits)
    {
        return Self { data, compressed_size, uncompressed_size, dict_bits }.do_decode();
    }

protected:
    explicit DecompressorBase(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size, uint16_t dict_bits)
        : dict_bits_ { dict_bits }
        , ibs_ { data, compressed_size }
        , out_(uncompressed_size)
    {
    }

    const uint16_t dict_bits_;
    InputBitString ibs_;

private:
    std::vector<uint8_t> out_;

    std::vector<uint8_t> do_decode()
    {
        for (uint32_t outpos = 0; outpos < out_.size();) {
            const auto sym = static_cast<Self&>(*this).decode_char();
            if (sym < 256) {
                out_[outpos++] = (uint8_t)sym;
                continue;
            }

            uint32_t len = (sym - 256) + min_match_len;
            uint32_t pos = static_cast<Self&>(*this).decode_pos();

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

        return std::move(out_);
    }
};

class StaticHuffDecompressor : public DecompressorBase<StaticHuffDecompressor> {
private:
    friend DecompressorBase;

    explicit StaticHuffDecompressor(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size, uint16_t dict_bits)
        : DecompressorBase { data, compressed_size, uncompressed_size, dict_bits }
        , ctab_ { NC, 12 }
        , ptab_ { uint16_t(dict_bits + 1), 8 }
    {
    }

    HuffTable ctab_;
    HuffTable ptab_;
    uint16_t blocksize_ = 0;

    uint16_t decode_char()
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

    uint32_t decode_pos() 
    {
        uint16_t p = ptab_.decode(ibs_);
        return 1 + (p ? (1 << (p - 1)) | ibs_.get(p - 1) : 0);
    }
};

class DynamicHuffDecompressor : public DecompressorBase<DynamicHuffDecompressor> {
private:
    friend DecompressorBase;

    explicit DynamicHuffDecompressor(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size, uint16_t dict_bits)
        : DecompressorBase { data, compressed_size, uncompressed_size, dict_bits }
        , ctree_ { 256 + 60 - 2 }
        , ptable_ { 0x40, 8 }
    {
      
        const uint8_t cc[] = { 0, 0, 0, 1, 3, 8, 12, 24, 16 };
        uint8_t clen[64];
        for (uint8_t sym = 0, len = 3; len < std::size(cc); ++len) {
            for (int j = 0; j < cc[len]; ++j)
                clen[sym++] = len;
        }

        ptable_.set_lengths(clen);
    }

    DynHuffTree ctree_;
    HuffTable ptable_;

    uint16_t decode_char()
    {
        return ctree_.decode(ibs_);
    }

    uint32_t decode_pos()
    {
        uint32_t ofs = ptable_.decode(ibs_) << 6;
        ofs |= ibs_.get(6);
        return ofs + 1;
    }
};

std::vector<uint8_t> decompress(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size, LhaMethod method)
{
    if (method == LHA_METHOD_LH0) {
        assert(uncompressed_size == compressed_size);
        return std::vector<uint8_t>(data, data + uncompressed_size);
    } else if (method == LHA_METHOD_LH1) {
        return DynamicHuffDecompressor::decode(data, compressed_size, uncompressed_size, window_bits_for_method(method));
    }

    return StaticHuffDecompressor::decode(data, compressed_size, uncompressed_size, window_bits_for_method(method));
}

std::vector<uint8_t> decompress(const std::vector<uint8_t>& data, const LhaHeader& hdr)
{
    const auto method = lha_method_from_id(hdr.compression_method);
    if (method == LHA_METHOD_UNKNOWN)
        throw std::runtime_error { std::format("Unsupported compression method {:5.5s}", (const char*)hdr.compression_method) };
    if (method == LHA_METHOD_DIR)
        return {};

    auto res = decompress(&data[hdr.compressed_offset], hdr.compressed_size, hdr.original_size, method);
    if (auto crc = crc16(res.data(), res.size()); crc != hdr.crc)
        throw std::runtime_error { std::format("CRC mismatch for {}. {:04x} <> {:04x}", hdr.filename.c_str(), crc, hdr.crc) };
    return res;
}

