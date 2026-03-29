#include "huffcoder.h"
#include "lhaconsts.h"
#include <vector>
#include <algorithm>
#include <iterator>
#include <cassert>
#include <stdexcept>

static std::vector<uint32_t> codelen_packing_merge(const std::vector<uint32_t>& prob, uint32_t max_len)
{
    // https://web.archive.org/web/20230430130342/https://create.stephan-brumme.com/length-limited-prefix-codes/
    struct node {
        uint32_t sym;
        uint32_t prob;
        bool merged;
        bool operator<(const node& n) const
        {
            return prob < n.prob;
        }
    };
    #if 0 
    auto pr = [](const std::vector<node>& nodes) {
        const int w = 4;
        for (size_t i = 0; i < nodes.size(); ++i)
            std::cout << std::setw(w) << nodes[i].sym << "  ";
        std::cout << '\n';
        for (size_t i = 0; i < nodes.size(); ++i)
            std::cout << std::setw(w) << nodes[i].prob << (nodes[i].merged ? '*' : ' ') << ' ';
        std::cout << '\n';
    };
    #endif

    // Phase 1

    std::vector<std::vector<node>> table;

    std::vector<node> orig_nodes;
    for (size_t i = 0; i < prob.size(); ++i)
        if (prob[i])
            orig_nodes.push_back({ static_cast<uint32_t>(i), prob[i], false });
    std::sort(orig_nodes.begin(), orig_nodes.end());
    //pr(orig_nodes);
    table.push_back(orig_nodes);

    std::vector<node> nodes = orig_nodes;
    for (uint32_t bit = 0; bit < max_len - 1; ++bit) {
        std::vector<node> packages;
        for (size_t i = 0; i + 2 <= nodes.size(); i += 2) {
            packages.push_back({ 0, nodes[i].prob + nodes[i + 1].prob, true });
        }
        nodes.clear();
        std::merge(orig_nodes.begin(), orig_nodes.end(), packages.begin(), packages.end(), std::back_inserter(nodes));
        // std::cout << "---------------------------------------------------\n";
        //pr(nodes);
        table.push_back(nodes);
    }

    // Phase 2
    std::vector<uint32_t> code_length(prob.size());
    for (uint32_t iter = 0, table_cnt = static_cast<uint32_t>(orig_nodes.size()) * 2 - 2; iter < max_len; ++iter) {
        uint32_t num_merged = 0;
        const auto& t = table[max_len - 1 - iter];
        for (size_t i = 0; i < table_cnt; ++i) {
            const auto& n = t[i];
            if (n.merged) {
                ++num_merged;
            } else {
                code_length[n.sym]++;
            }
        }
        table_cnt = 2 * num_merged;
    }
    return code_length;
}

static std::vector<uint32_t> assign_codes(const std::vector<uint32_t>& codelen)
{
    std::vector<uint32_t> lencnt;
    for (const auto l : codelen) {
        if (l >= lencnt.size())
            lencnt.resize(l + 1);
        lencnt[l]++;
    }
    std::vector<uint32_t> next(lencnt.size());
    for (size_t i = 2; i < next.size(); ++i)
        next[i] = (next[i - 1] + lencnt[i - 1]) << 1;

    std::vector<uint32_t> codes(codelen.size());
    for (uint32_t i = 0; i < codelen.size(); ++i)
        codes[i] = next[codelen[i]]++;

    return codes;
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

static uint32_t effective_table_length(const std::vector<uint32_t>& tab)
{
    uint32_t l;
    for (l = (uint32_t)tab.size(); l-- && !tab[l];)
        ;
    return l + 1;
}


HuffCoder::HuffCoder(const std::vector<uint32_t>& freq)
    : num_sym_ { effective_table_length(freq) }
{
    clen_ = codelen_packing_merge(freq, max_code_bits);
    code_ = assign_codes(clen_);
}

void HuffCoder::encode(OutputBitString& obs, uint16_t sym) const
{
    assert(sym < num_sym_);
    if (!clen_[sym]) {
        assert(!clen_[num_sym_ - 1]);
        return;
    }
    obs.put(code_[sym], clen_[sym]);
}

void HuffCoder::encode_table_c(OutputBitString& obs) const
{
    // Syms:
    // 0 => 1 zero
    // 1 => 3 + 4 bits zeros (3..18)
    // 2 => 20 + CBIT (9) bits zeros (20..)
    // 3 => length 1
    // 4 => length 2
    // ...


    // TODO: Special case is possible if all codes have the same length (or there is only one symbol)
    assert(num_sym_);
    if (!clen_[num_sym_ - 1]) {
        assert(std::all_of(clen_.begin(), clen_.end(), [](uint32_t len) { return len == 0; }));
        throw std::runtime_error { "TODO: Special case for only one char" };
    }

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

void HuffCoder::encode_table_p(OutputBitString& obs, uint32_t window_bits) const
{
    assert(num_sym_);
    assert(num_sym_ <= NT);
    const auto tbits = window_bits < 14 ? 4 : 5;

    // Special case: Only one symbol
    if (!clen_[num_sym_ - 1]) {
        assert(std::all_of(clen_.begin(), clen_.end(), [](uint32_t len) { return len == 0; }));
        obs.put(0, tbits);
        obs.put(num_sym_ - 1, tbits);
        return;
    }

    obs.put(num_sym_, tbits);
    for (uint32_t i = 0; i < num_sym_; ++i)
        encode_pt_len(obs, clen_[i]);
}
