#include "compress.h"
#include "lz.h"
#include "obs.h"
#include "huffcoder.h"
#include "dynhuff.h"
#include "lhaconsts.h"
#include <cassert>

static void encode_block(OutputBitString& obs, const LzNode* lz, uint16_t size, uint16_t window_bits)
{
    assert(size);
    std::vector<uint32_t> c_freq(NC);
    std::vector<uint32_t> p_freq(NT);

    for (uint32_t i = 0; i < size; ++i) {
        const auto& n = lz[i];
        c_freq[n.code]++;
        if (n.code >= 256)
            p_freq[p_len(n.ofs)]++;
    }

    HuffCoder ccoder { c_freq };
    HuffCoder pcoder { p_freq };

    obs.put(size, 16);
    ccoder.encode_table_c(obs);
    pcoder.encode_table_p(obs, window_bits);

    while (size--) {
        const auto& n = *lz++;
        ccoder.encode(obs, n.code);
        if (n.code >= 256) {
            const auto pl = p_len(n.ofs);
            pcoder.encode(obs, pl);
            if (pl)
                obs.put(n.ofs & ~(1 << (pl - 1)), pl - 1);
        }
    }
}

static void encode_lh1(OutputBitString& obs, const std::vector<LzNode>& lz)
{
    DynHuffTree ctree { lh1_nchars };
    HuffCoder pcoder { lh1_p_codelen, sizeof(lh1_p_codelen) };

    for (const auto [code, ofs] : lz) {
        ctree.encode(obs, code);
        if (code < 256)
            continue;
        pcoder.encode(obs, ofs >> 6);
        obs.put(ofs & 0x3f, 6);
    }
}

std::vector<uint8_t> compress(const std::vector<LzNode>& lz, LhaMethod method)
{
    OutputBitString obs;
    uint16_t window_bits = window_bits_for_method(method);
    if (method == LHA_METHOD_LH1) {
        encode_lh1(obs, lz);
    } else {
        // TODO: Rather than a fixed block size, check the frequency and switch if it changes "enough"
        for (size_t pos = 0; pos < lz.size();) {

            // Max size is 65535, but that gives poor compression if the frequencies change
            const auto rem = lz.size() - pos;
            constexpr size_t max_block = 8192;
            size_t here = rem;
            if (here >= 2 * max_block)
                here = max_block;


            encode_block(obs, &lz[pos], (uint16_t)here, window_bits);
            pos += here;
        }
    }
    return obs.finish();
}

std::vector<uint8_t> compress(const uint8_t* data, uint32_t size, LhaMethod method)
{
    const uint32_t max_match = method == LHA_METHOD_LH1 ? 60 : max_match_len;
    return compress(lz_build(data, size, max_match, window_bits_for_method(method)), method);
}
