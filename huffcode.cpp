#include "huffcode.h"
#include <vector>
#include <numeric>
#include <algorithm>
#include <functional>
#include <iterator>
#include <cassert>

std::vector<uint32_t> codelen_huff(const std::vector<uint32_t>& prob)
{
    const auto num_symbols = prob.size();
    assert(num_symbols > 1);

    std::vector<uint32_t> codelen(num_symbols);

    std::vector<uint32_t> p = prob;
    std::vector<uint32_t> l, r;

    std::vector<uint32_t> heap;
    for (size_t i = 0; i < prob.size(); ++i)
        if (prob[i])
            heap.push_back(static_cast<uint32_t>(i));

    auto node_compare = [&p](uint32_t l, uint32_t r) {
        return p[l] > p[r];
    };

    std::make_heap(heap.begin(), heap.end(), node_compare);
    while (heap.size() > 1) {
        auto pop = [&]() {
            std::pop_heap(heap.begin(), heap.end(), node_compare);
            auto res = heap.back();
            heap.pop_back();
            // std::cout << "popped " << res << " prob = " << p[res] << "\n";
            return res;
        };
        auto ln = pop();
        auto rn = pop();

        heap.push_back(static_cast<uint32_t>(p.size()));
        l.push_back(ln);
        r.push_back(rn);
        p.push_back(p[ln] + p[rn]);
        // std::cout << "Inserted " << heap.back() << " " << p.back() << "\n";
        std::push_heap(heap.begin(), heap.end(), node_compare);
    }
    assert(p.back() == std::accumulate(prob.cbegin(), prob.cend(), uint32_t { 0 }));

    std::function<void(size_t, uint32_t)> get_code_len;
    get_code_len = [&](size_t idx, uint32_t len) {
        if (idx < num_symbols) {
            codelen[idx] = len;
            return;
        }
        get_code_len(l[idx - num_symbols], len + 1);
        get_code_len(r[idx - num_symbols], len + 1);
    };
    get_code_len(p.size() - 1, 0);

    return codelen;
}

std::vector<uint32_t> codelen_packing_merge(const std::vector<uint32_t>& prob, uint32_t max_len)
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
        uint32_t symbol = 0, num_merged = 0;
        const auto& t = table[max_len - 1 - iter];
        for (size_t i = 0; i < table_cnt; ++i) {
            const auto& n = t[i];
            if (n.merged) {
                ++num_merged;
            } else {
                code_length[n.sym]++;
                ++symbol;
            }
        }
        table_cnt = 2 * num_merged;
    }
    return code_length;
}

std::vector<uint32_t> assign_codes(const std::vector<uint32_t>& codelen)
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


