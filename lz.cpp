#include "lz.h"
#include "lhaconsts.h"
#include <cassert>
#include <algorithm>
#include <cstring>

#define max_match_len XXX

// Heavily inspired by similar class in in shrinkler
class MatchFinder {
public:
    MatchFinder(const uint8_t* src, uint32_t len, uint32_t max_match, uint16_t window_bits)
        : len_ { len }
        , pos_(len)
        , rev_(len)
        , lcp_(len)
        , max_offset_ { 1U << window_bits }
        , max_match_ { max_match }
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

    uint32_t matches(uint32_t start_pos, uint32_t* mpos, uint32_t* mlen, uint32_t max_matches)
    {
        uint32_t nmatches;
        start(start_pos);
        for (nmatches = 0; nmatches < max_matches;) {
            if (!next_match(*mpos, *mlen))
                break;
            if (start_pos - *mpos > max_offset_)
                continue;
            if (*mlen > max_match_)
                *mlen = max_match_;
            ++mlen;
            ++mpos;
            ++nmatches;
        }
        return nmatches;
    }

private:
    uint32_t len_;
    std::vector<uint32_t> pos_; // sorted suffix array
    std::vector<uint32_t> rev_; // inverse mapping (from string position to index in "pos")
    std::vector<uint32_t> lcp_; // longest common prefix array

    uint32_t left_idx_ = 0, left_len_ = 0;
    uint32_t right_idx_ = 0, right_len_ = 0;
    uint32_t cur_pos_ = 0;
    const uint32_t max_offset_;
    const uint32_t max_match_;

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
};

static constexpr uint32_t p_cost = 4;
static constexpr uint32_t sym_cost = 9;

static inline uint32_t offset_cost(uint32_t pos, uint32_t match_pos)
{
    // More accurate model of pcost (based on observed frequency) doesn't improve ratio
    return p_len(pos - match_pos - 1) + p_cost;
}

#include "huffcoder.h"
std::vector<LzNode> lz_build(const uint8_t* data, uint32_t size, uint32_t max_match, uint16_t window_bits, uint32_t max_matches)
{
    if (!size)
        return {};

    std::vector<uint32_t> lfreq(256);
    for (uint32_t i = 0; i < size; ++i)
        lfreq[data[i]]++;
    HuffCoder hc { lfreq };
    std::vector<uint8_t> litcost(256);
    for (uint32_t i = 0; i < 256; ++i)
        litcost[i] = uint8_t(hc.code_length()[i] + 2);


    MatchFinder mf { data, size, max_match, window_bits };

    struct CostNode {
        uint16_t code = 0;
        uint32_t mpos = 0;
        uint64_t cost = UINT64_MAX;
    };
    std::vector<CostNode> cn(size + 1);
    cn[0].cost = 0;
    std::vector<uint32_t> mpos(max_matches);
    std::vector<uint32_t> mlen(max_matches);

    for (uint32_t pos = 0; pos < size; ++pos) {
        if (auto lc = cn[pos].cost + litcost[data[pos]]; lc < cn[pos + 1].cost) {
            cn[pos + 1].cost = lc;
            cn[pos + 1].code = data[pos];
        }

        const uint32_t nmatches = mf.matches(pos, &mpos[0], &mlen[0], max_matches);
        for (uint32_t i = 0; i < nmatches; ++i) {
            const auto base_cost = cn[pos].cost + /*sym_cost + */offset_cost(pos, mpos[i]);
            for (uint32_t j = min_match_len; j <= mlen[i]; ++j) {
                auto& n = cn[pos + j];
                const auto match_cost = base_cost + 7 + p_len(j - min_match_len);
                if (match_cost < n.cost) {
                    n.cost = match_cost;
                    n.code = (uint16_t)(256 + j - min_match_len);
                    n.mpos = mpos[i];
                }
            }
        }
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