#include "lz.h"
#include "lhaconsts.h"
#include <cassert>

class MatchFinder {
public:
    explicit MatchFinder(const uint8_t* data, size_t size, uint16_t window_bits)
        : window_size { 1U << window_bits }
        , window_mask { window_size - 1 }
        , data_ { data }
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

        if (data_ + pos + min_match_len > end_)
            return false;

        const int max_search = 64;
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
    const uint32_t window_size;
    const uint32_t window_mask;
    const uint8_t* const data_;
    const uint8_t* const end_;
    std::vector<int> table_;
    std::vector<int> chain_;

    int hash_from_pos(uint32_t pos)
    {
        if (data_ + pos + min_match_len > end_)
            return 0;
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

std::vector<LzNode> lz_build(const uint8_t* data, uint32_t size, uint16_t window_bits)
{
    if (!size)
        return {};

    std::vector<LzNode> res;
    const uint32_t window_size = 1 << window_bits;

    auto emit_lit = [&res](uint8_t ch) {
        res.push_back(LzNode { ch, 0 });
    };

    auto emit_match = [&res, window_size](uint32_t len, uint16_t ofs) {
        assert(len >= min_match_len && len <= max_match_len);
        assert(ofs < window_size);
        res.push_back(LzNode { uint16_t(len - min_match_len + 256), ofs });
    };

    emit_lit(*data);

    MatchFinder mf { data, size, window_bits };

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

