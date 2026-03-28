#include <print>
#include <stdexcept>
#include <vector>
#include <cassert>
#include "fileio.h"
#include "util.h"
#include "lhaconsts.h"
#include "obs.h"

static constexpr uint16_t window_bits = 13; // LH5
static constexpr uint32_t window_size = 1U << window_bits;
static constexpr uint32_t window_mask = window_size - 1;

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


void test_file(const std::string& filename)
{
    auto data = read_file(filename);
    const auto res = build_lz(data.data(), (uint32_t)data.size());
    test_lz(data.data(), (uint32_t)data.size(), res);
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
