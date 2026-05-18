#include "lz.h"
#include "lhaconsts.h"
#include <cassert>
#include <algorithm>
#include <cstring>

#if 1

struct Match {
    uint16_t len;
    uint16_t ofs;
};

class SimpleMatchFinder {
public:
    explicit SimpleMatchFinder(const uint8_t* src, uint32_t len, uint32_t max_match, uint16_t window_bits) 
        : data_ { src }
        , len_ { len }
        , max_match_ { max_match }
        , window_mask_ { (1U << window_bits) - 1 }
        , chain_(window_mask_ + 1, INT32_MIN)
        , table_(window_mask_ + 1, INT32_MIN)
    { 
    }

    void update(uint32_t pos)
    {
        if (pos + min_match_len > len_)
            return;
        const uint32_t key = hash(pos);
        chain_[pos & window_mask_] = table_[key];
        assert(table_[key] != (int32_t)pos);
        table_[key] = pos;
    }

    Match find(uint32_t pos)
    {
        if (pos + min_match_len > len_)
            return {};
        const auto min_pos = std::max(0, int32_t(pos - window_mask_ - 1));
        const uint8_t first_chars[3] = { data_[pos], data_[pos + 1], data_[pos + 2] };
        uint32_t max_len = 0;
        uint32_t match_pos = 0;
        for (int32_t next = table_[hash(pos)]; next >= min_pos; next = chain_[next & window_mask_]) {
            if (data_[next] != first_chars[0] || data_[next + 1] != first_chars[1] || data_[next + 2] != first_chars[2])
                continue;

            uint32_t l = 3;
            while (pos + l < len_ && l < max_match_ && data_[next + l] == data_[pos + l])
                ++l;

            if (l > max_len) {
                max_len = l;
                match_pos = next;
                if (l == max_match_)
                    break;
            }

        }
        return { (uint16_t)max_len, uint16_t(pos - match_pos - 1) };
    }

private:
    const uint8_t* const data_;
    const uint32_t len_;
    const uint32_t max_match_;
    const uint32_t window_mask_;

    std::vector<int32_t> chain_;
    std::vector<int32_t> table_;
    
    uint32_t hash(uint32_t pos)
    {
#if 1
        uint32_t h = (data_[pos] << 16) | (data_[pos + 1] << 8) | data_[pos + 2];
        return ((h * 0x1E35A7BD) >> 16) & window_mask_;
#else
        uint32_t hash = 5381;
        hash = (hash * 33) ^ data_[pos + 0];
        hash = (hash * 33) ^ data_[pos + 1];
        hash = (hash * 33) ^ data_[pos + 2];
        return hash & window_mask_;
#endif
    }
};



/*
  170782   21538 87.3% 03-Apr-23 13:51:50  80croc.def
  112384   42481 62.2% 03-Apr-23 13:51:50  BLOX1.DAT
  570307  519571  8.8% 02-Apr-23 11:26:08  data.adpcm
    3475     733 78.9% 22-Sep-24 08:14:10  Green Eggs and Ham.txt
  267264  126757 52.5% 03-Apr-23 13:51:50  jp2_000
   34468   17429 49.4% 03-Apr-23 13:51:50  jp2_001
   51100   21803 57.3% 03-Apr-23 13:51:50  jp2_002
  118784   68438 42.3% 03-Apr-23 13:51:50  MAIN.BIN
      96      20 79.1% 04-Apr-23 06:16:18  simple.txt
   14984    3378 77.4% 12-Apr-23 08:26:52  sprite_intro
  245720   57664 76.5% 03-Apr-23 13:51:50  Zombies.SHP
*/

// ../test_comp/80croc.def         21493 170782 12.59% -lh5-
// ../test_comp/BLOX1.DAT          42486 112384 37.80% -lh5-
// ../test_comp/data.adpcm        521542 570307 91.45% -lh5-
// ../test_comp/Green Eggs and Ham.txt    732   3475 21.06% -lh5-
// ../test_comp/jp2_000           126840 267264 47.46% -lh5-
// ../test_comp/jp2_001            17420  34468 50.54% -lh5-
// ../test_comp/jp2_002            21773  51100 42.61% -lh5-
// ../test_comp/MAIN.BIN           68011 118784 57.26% -lh5-
// ../test_comp/simple.txt            20     96 20.83% -lh5-
// ../test_comp/skykule.rmsh.mat      17    580 2.93% -lh5-
// ../test_comp/sprite_intro        3369  14984 22.48% -lh5-
// ../test_comp/Zombies.SHP        57439 245720 23.38% -lh5-
// ../test_comp/80croc.def         22687 170782 13.28% -lh1-
// ../test_comp/80croc.def         22349 170782 13.09% -lh4-
// ../test_comp/80croc.def         17544 170782 10.27% -lh7-

std::vector<LzNode> lz_build(const uint8_t* data, uint32_t size, uint32_t max_match, uint16_t window_bits, uint32_t max_matches)
{
    (void)max_matches;

    std::vector<LzNode> res;

    SimpleMatchFinder mf { data, size, max_match, window_bits };

    auto literal = [&](uint32_t pos) {
        res.push_back(LzNode { uint16_t(data[pos]), 0 });
    };

    Match last {};
    for (uint32_t pos = 0; pos < size;) {
        Match m = mf.find(pos);
        mf.update(pos);

        if (m.len > last.len) {
            if (last.len)
                literal(pos - 1);
            last = m;
            ++pos;
        } else if (!last.len) {
            literal(pos);
            ++pos;
        } else {
            res.push_back(LzNode { uint16_t(last.len - min_match_len + 256), last.ofs });
            const auto end_pos = (pos - 1) + last.len;
            while (++pos != end_pos)
                mf.update(pos);

            last = {};
        }
    }
    return res;

}

#else
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
#endif