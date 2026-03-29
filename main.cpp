#include <print>
#include <stdexcept>
#include <vector>
#include <cassert>
#include "fileio.h"
#include "util.h"
#include "lhaconsts.h"
#include "obs.h"
#include "huffcode.h"

static constexpr uint16_t window_bits = 13; // LH5
static constexpr uint32_t window_size = 1U << window_bits;
static constexpr uint32_t window_mask = window_size - 1;

static uint16_t p_len(uint32_t n)
{
    uint16_t l = 0;
    while (n) {
        l++;
        n >>= 1;
    }
    return l;
}

struct LzNode {
    uint16_t code;
    uint16_t ofs;
};

class MatchFinder {
public:
    explicit MatchFinder(const uint8_t* data, size_t size)
        : data_ { data }
        , end_ { data + size }
        , table_ ( window_size, -(int)window_size )
        , chain_ ( window_size, -(int)window_size )
    {
    }

    void add(uint32_t pos)
    {
        const int key = hash_from_pos(pos);
        chain_[pos & window_mask] = table_[key];
        table_[key] = pos;
    }

    bool longest_match(uint32_t pos, uint32_t& mpos, uint32_t& mlen)
    {
        int next = table_[hash_from_pos(pos)];
        const int min_pos = (int)(pos - window_size);

        const int max_search = 4;
        mlen = 0;
        for (int nsearch = 0; nsearch < max_search && next > min_pos; ++nsearch) {
            const uint32_t len = match_len(pos, next);
            if (len > mlen) {
                mlen = len;
                mpos = next;
            }
            next = chain_[next & window_mask];
        }
        return mlen >= min_match_len;
    }

private:
    const uint8_t* const data_;
    const uint8_t* const end_;
    std::vector<int> table_;
    std::vector<int> chain_;

    int hash_from_pos(uint32_t pos)
    {
        uint32_t h = data_[pos] | data_[pos + 1] << 8 | data_[pos + 2] << 16;
        // TODO better hash
        h = (h >> 16) ^ h;
        return static_cast<int>(h & window_mask);
    }

    uint32_t match_len(uint32_t cur_pos, uint32_t search_pos)
    {
        uint32_t len = 0;
        assert(search_pos < cur_pos);
        
        for (const uint8_t* pos = &data_[cur_pos], *sp = &data_[search_pos]; pos < end_ && len < max_match_len; ++len) {
            if (*pos++ != *sp++)
                break;
        }
        return len;
    }
};

std::vector<LzNode> build_lz(const uint8_t* data, uint32_t size)
{
    if (!size)
        return {};

    std::vector<LzNode> res;

    auto emit_lit = [&res](uint8_t ch) {
        res.push_back(LzNode { ch, 0 });
    };

    auto emit_match = [&res](uint32_t len, uint16_t ofs) {
        assert(len >= min_match_len && len <= max_match_len);
        assert(ofs < window_size);
        res.push_back(LzNode { uint16_t(len - min_match_len + 256), ofs });
    };

    emit_lit(*data);

    MatchFinder mf { data, size };

    for (uint32_t pos = 1; pos < size;) {
        uint32_t mpos, mlen;
        if (!mf.longest_match(pos, mpos, mlen)) {
            emit_lit(data[pos]);
            mf.add(pos++);
            continue;
        }
        emit_match(mlen, uint16_t(pos - mpos - 1));
        for (uint32_t i = 0; i < mlen; ++i)
            mf.add(pos++);
    }

    return res;
}

void test_lz(const uint8_t* data, uint32_t size, const std::vector<LzNode>& lz)
{
    std::vector<uint8_t> out;

    for (const auto& n : lz) {
        if (n.code < 256) {
            out.push_back((uint8_t)n.code);
            continue;
        }
        uint32_t len = n.code - 256 + min_match_len;
        uint32_t ofs = n.ofs + 1;
        if (ofs > out.size())
            throw std::runtime_error {std::format("Invalid offset in LZ stream {} (max {})", ofs, out.size()) };
        while (len--)
            out.push_back(out[out.size() - ofs]);
    }

    if (out.size() != size)
        throw std::runtime_error { std::format("Decompression failed size: {} <> {}", out.size(), size) };
    if (!memcmp(data, out.data(), size))
        return;

    std::println("Decompressed failed!");
    exit(1);
}

void print_table(const std::vector<uint32_t>& code_len_)
{
    static constexpr bool debug = false;
    const uint32_t num_syms_ = (uint32_t)code_len_.size();

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

    for (uint32_t sym = 0; sym < num_syms_; ++sym) {
        const uint32_t clen = code_len_[sym];
        if (!clen)
            continue;
        std::println("{:03x} {:0{}b}", sym, start[clen] >> (16 - clen), clen);
        start[clen] += weight[clen];
    }
}

class HuffCoder {
public:
    explicit HuffCoder(const std::vector<uint32_t>& freq)
        : num_sym_ { effective_table_length(freq) }
    {
        clen_ = codelen_packing_merge(freq, max_code_bits);
        code_ = assign_codes(clen_);
    }

    void encode(OutputBitString& obs, uint16_t sym) const
    {
        assert(sym < num_sym_);
        obs.put(code_[sym], clen_[sym]);
    }

    void print_codes() const
    {
        for (uint32_t i = 0; i < num_sym_; ++i)
            if (clen_[i])
                std::println("{:03X} {:0{}b}", i, code_[i], clen_[i]);
    }

    void encode_table_c(OutputBitString& obs)
    {
        // Syms:
        // 0 => 1 zero
        // 1 => 3 + 4 bits zeros (3..18)
        // 2 => 20 + CBIT (9) bits zeros (20..)
        // 3 => length 1
        // 4 => length 2
        // ...


        // TODO: Special case is possible if all codes have the same length
        if (!num_sym_)
            throw std::runtime_error { "TODO: Empty c-table?!" };

        std::vector<uint32_t> t_freq(NT);
        uint32_t zero_run = 0;
        std::vector<uint32_t> t_syms;
        std::vector<uint32_t> t_extra;

        for (uint32_t i = 0; i < num_sym_; ++i) {
            auto put_sym = [&](uint8_t sym, uint32_t extra = 0) {
                t_freq[sym]++;
                t_syms.push_back(sym);
                t_extra.push_back(extra);
            };

            if (clen_[i]) {
                if (zero_run) {
                    // TODO: Maybe cut-off points could be improved
                    if (zero_run >= 20) {
                        put_sym(2, zero_run - 20);
                    } else if (zero_run == 19) {
                        put_sym(1, 15);
                        put_sym(0);
                    } else if (zero_run >= 3) {
                        put_sym(1, zero_run - 3);
                    } else {
                        while (zero_run--)
                            put_sym(0);
                    }
                    zero_run = 0;
                }
                put_sym((uint8_t)(clen_[i] + 2));
            } else {
                ++zero_run;
            }
        }
        assert(zero_run == 0);

        HuffCoder t_coder { t_freq };
        encode_t_lens(obs, t_coder.clen_, t_coder.num_sym_);

        obs.put(num_sym_, CBIT);
        for (size_t i = 0; i < t_syms.size(); ++i) {
            const auto sym = t_syms[i];
            t_coder.encode(obs, (uint16_t)sym);
            if (sym == 1)
                obs.put(t_extra[i], 4);
            else if (sym == 2)
                obs.put(t_extra[i], CBIT);
        }
    }

    void encode_table_p(OutputBitString& obs)
    {
        assert(num_sym_ <= NT);
        obs.put(num_sym_, window_bits < 14 ? 4 : 5);
        for (uint32_t i = 0; i < num_sym_; ++i)
            encode_pt_len(obs, clen_[i]);
    }

private:
    const uint32_t num_sym_;
    std::vector<uint32_t> clen_;
    std::vector<uint32_t> code_;

    static void encode_t_lens(OutputBitString& obs, const std::vector<uint32_t>& clen, uint32_t num_sym)
    {
        assert(num_sym <= NT);
        obs.put(num_sym, TBIT);
        for (uint32_t i = 0; i < num_sym && i < 3U; ++i)
            encode_pt_len(obs, clen[i]);
        uint32_t nz = 0;
        for (uint32_t i = 3; i < num_sym && i < 6 && clen[i] == 0; ++i)
            ++nz;
        obs.put(nz, 2);
        for (uint32_t i = 3 + nz; i < num_sym; ++i)
            encode_pt_len(obs, clen[i]);
    }

    static void encode_pt_len(OutputBitString& obs, uint32_t len)
    {
        if (len < 7) {
            obs.put(len, 3);
            return;
        }
        obs.put(7, 3);
        len -= 7;
        while (len--)
            obs.put(1, 1);
        obs.put(0, 1);
    }

    static uint32_t effective_table_length(const std::vector<uint32_t>& tab)
    {
        uint32_t l;
        for (l = (uint32_t)tab.size(); l-- && !tab[l];)
            ;
        return l + 1;
    }
};

void encode_block(OutputBitString& obs, const LzNode* lz, uint16_t size)
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
    pcoder.encode_table_p(obs);

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

std::vector<uint8_t> encode_lh5(const std::vector<LzNode>& lz)
{
    OutputBitString obs;
    // TODO: Maybe it's worth doing smaller blocks once frequency of codes changes "enough"
    for (size_t pos = 0; pos < lz.size();) {
        const auto here = std::min(size_t(65535), lz.size() - pos);
        encode_block(obs, &lz[pos], (uint16_t)here);
        pos += here;
    }
    return obs.finish();
}

#include "decompress.h"
void test_file(const std::string& filename)
{
    auto data = read_file(filename);
    const auto lz = build_lz(data.data(), (uint32_t)data.size());
    test_lz(data.data(), (uint32_t)data.size(), lz);
    const auto encoded = encode_lh5(lz);
    const auto decoded = decompress(encoded.data(), (uint32_t)encoded.size(), (uint32_t)data.size(), LHA_METHOD_LH5);

    if (decoded != data)
        throw std::runtime_error { std::format("Wrong data decoded for {}", filename) };

}

#include <filesystem>
void test_dir(const std::string& dir_path)
{
    namespace fs = std::filesystem;
    for (const auto& e : fs::recursive_directory_iterator { dir_path })
        test_file(e.path().string());
}


#include "ibs.h"
void test_obs()
{
    OutputBitString obs {};
    obs.put(0xABCD, 16);
    obs.put(42, 7);
    obs.put(123, 8);
    const auto res = obs.finish();
    InputBitString ibs { res.data(), (uint32_t)res.size() };
    assert(ibs.get(16) == 0xABCD);
    assert(ibs.get(7) == 42);
    assert(ibs.get(8) == 123);
}

int main()
{
    try {
        test_obs();
        const std::string dir = "../test_comp/";
        test_file(dir + "Green Eggs and Ham.txt");
        test_dir(dir);
    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
