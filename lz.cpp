#include "lz.h"
#include "lhaconsts.h"
#include <cassert>
#include <algorithm>
#include <cstring>

#if 0
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
        if (data_ + pos + min_match_len > end_)
            return false;

        int next = table_[hash_from_pos(pos)];
        const int min_pos = (int)(pos - window_size);

        const int max_search = 64;
        mlen = 0;
        for (int nsearch = 0; nsearch < max_search && next > min_pos; ++nsearch) {
            const uint32_t len = match_len(pos, next);
            if (len > mlen) {
                mlen = len;
                mpos = next;
                if (mlen == max_match_len)
                    break;
            }
            next = chain_[next & window_mask];
        }
        return mlen >= min_match_len;
    }

    uint32_t matches(uint32_t pos, uint32_t* mpos, uint32_t* mlen, uint32_t max_matches)
    {
        if (data_ + pos + min_match_len > end_)
            return 0;

        int next = table_[hash_from_pos(pos)];
        const int min_pos = (int)(pos - window_size);

        uint32_t nmatches;
        for (nmatches = 0; nmatches < max_matches && next > min_pos;) {
            const uint32_t len = match_len(pos, next);
            if (len >= min_match_len) {
                *mlen++ = len;
                *mpos++ = next;
                ++nmatches;
            }
            next = chain_[next & window_mask];
        }

        return nmatches;
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

std::vector<LzNode> lz_build_fast(const uint8_t* data, uint32_t size, uint16_t window_bits)
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
        
        auto mofs = uint16_t(pos - mpos - 1); 
        mf.add(pos);
        if (uint32_t mpos2, mlen2; mf.longest_match(pos + 1, mpos2, mlen2) && mlen2 > mlen) {
            emit_lit(data[pos++]);
            mlen = mlen2;
            mofs = uint16_t(pos - mpos2 - 1);
            mf.add(pos);
        }

        emit_match(mlen, mofs);
        for (uint32_t i = 1; i < mlen; ++i)
            mf.add(pos + i);
        pos += mlen;
    }

    return res;
}

#else
// Heavily inspired by MatchFinder in shrinkler
class MatchFinder {
    uint32_t len_;
    std::vector<uint32_t> pos_; // sorted suffix array
    std::vector<uint32_t> rev_; // inverse mapping (from string position to index in "pos")
    std::vector<uint32_t> lcp_; // longest common prefix array

    uint32_t left_idx_ = 0, left_len_ = 0;
    uint32_t right_idx_ = 0, right_len_ = 0;
    uint32_t cur_pos_ = 0;
    const uint32_t max_offset;
public:

    MatchFinder(const uint8_t* src, uint32_t len, uint16_t window_bits)
        : len_ { len }
        , pos_(len)
        , rev_(len)
        , lcp_(len)
        , max_offset { 1U << window_bits }
    {
        for (uint32_t i = 0; i < len; ++i)
            pos_[i] = i;
        std::sort(pos_.begin(), pos_.end(), [&](uint32_t l, uint32_t r) {
            const auto res = std::memcmp(src + l, src + r, len - std::max(l, r));
            if (res < 0)
                return true;
            return res == 0 && l > r;
        });
        for (uint32_t i = 0; i < len; ++i)
            rev_[pos_[i]] = i;

        // Construct LCP array using Kasai's method
        uint32_t l = 0;
        for (uint32_t i = 0; i < len; ++i) {
            uint32_t r = rev_[i];
            if (r + 1 == len)
                continue;
            uint32_t j = pos_[r + 1];
            uint32_t m = len - std::max(i, j);
            // l - 1 characters match (invariant)
            while (l < m && src[i + l] == src[j + l])
                ++l;
            lcp_[r] = l;
            if (l)
                --l; // Delete character from the beginning of the string
        }
    }

    void start(uint32_t p)
    {
        cur_pos_ = p;
        left_idx_ = right_idx_ = rev_[p];
        left_len_ = right_len_ = len_ - p;
        move_left();
        move_right();
    }

    void move_left()
    {
        while (left_len_ >= min_match_len) {
            if (!left_idx_) {
                left_len_ = 0;
                return;
            }
            left_len_ = std::min(left_len_, lcp_[--left_idx_]);
            if (left_len_ < min_match_len)
                return;
            uint32_t p = pos_[left_idx_];
            if (p < cur_pos_)
                return;
        }
    }

    void move_right()
    {
        for (;;) {
            right_len_ = std::min(right_len_, lcp_[right_idx_]);
            if (right_len_ < min_match_len)
                return;
            if (right_idx_ + 1 == len_) {
                right_len_ = 0;
                return;
            }
            uint32_t p = pos_[++right_idx_];
            if (p < cur_pos_)
                return;
        }
    }

    bool next_match(uint32_t& match_pos, uint32_t& match_len)
    {
        if (left_len_ > right_len_ || (left_len_ == right_len_ && pos_[left_idx_] > pos_[right_idx_])) {
            if (left_len_ < min_match_len)
                return false;
            match_len = left_len_;
            match_pos = pos_[left_idx_];
            move_left();
        } else {
            if (right_len_ < min_match_len)
                return false;
            match_len = right_len_;
            match_pos = pos_[right_idx_];
            move_right();
        }
        return true;
    }

    uint32_t matches(uint32_t start_pos, uint32_t* mpos, uint32_t* mlen, uint32_t max_matches)
    {
        uint32_t nmatches;
        start(start_pos);
        for (nmatches = 0; nmatches < max_matches;) {
            if (!next_match(*mpos, *mlen))
                break;
            if (start_pos - *mpos > max_offset)
                continue;
            if (*mlen > max_match_len)
                *mlen = max_match_len;
            ++mlen;
            ++mpos;
            ++nmatches;
        }
        return nmatches;
    }

    void add(uint32_t) {}
};
#endif

static constexpr uint32_t p_cost = 4;
static constexpr uint32_t sym_cost = 9;

static inline uint32_t lit_cost(uint8_t ch)
{
    if (ch == 0x00 || ch == 0xFF)
        return sym_cost - 2;
    return sym_cost;
}

static inline uint32_t offset_cost(uint32_t pos, uint32_t match_pos)
{
    return p_len(pos - match_pos - 1) + p_cost;
}

std::vector<LzNode> lz_build(const uint8_t* data, uint32_t size, uint16_t window_bits)
{
    if (!size)
        return {};

    MatchFinder mf { data, size, window_bits };

    struct CostNode {
        uint16_t code = 0;
        uint32_t mpos = 0;
        uint64_t cost = UINT64_MAX;
    };
    std::vector<CostNode> cn(size + 1);
    cn[0].cost = 0;

    for (uint32_t pos = 0; pos < size; ++pos) {
        if (auto lc = cn[pos].cost + lit_cost(data[pos]); lc < cn[pos + 1].cost) {
            cn[pos + 1].cost = lc;
            cn[pos + 1].code = data[pos];
        }

        static constexpr uint32_t max_matches = 64;
        uint32_t mpos[max_matches], mlen[max_matches];
        uint32_t nmatches = mf.matches(pos, mpos, mlen, max_matches);

        for (uint32_t i = 0; i < nmatches; ++i) {
            const auto match_cost = cn[pos].cost + sym_cost + offset_cost(pos, mpos[i]);
            for (uint32_t j = min_match_len; j <= mlen[i]; ++j) {
                auto& n = cn[pos + j];
                if (match_cost < n.cost) {
                    n.cost = match_cost;
                    n.code = (uint16_t)(256 + j - min_match_len);
                    n.mpos = mpos[i];
                }
            }
        }

        mf.add(pos);
    }

    std::vector<LzNode> res;

    for (uint32_t pos = size; pos;) {
        const auto& n = cn[pos];
        LzNode lz {};
        lz.code = n.code;
        if (n.code < 256) {
            --pos;
        } else {
            const auto len = n.code - 256 + min_match_len;
            assert(pos >= len);
            pos -= len;
            lz.ofs = uint16_t(pos - n.mpos - 1);
        }
        res.push_back(lz);
    }

    std::reverse(res.begin(), res.end());

    return res;
}