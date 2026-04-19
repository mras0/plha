#include "dynhuff.h"
#include "ibs.h"
#include "obs.h"
#include <algorithm>

DynHuffTree::DynHuffTree(uint16_t num_symbols)
    : num_symbols_ { num_symbols }
    , tree_size { uint16_t(num_symbols_ * 2 - 1) }
    , root { uint16_t(tree_size - 1) }
    , freq_(tree_size + 1)
    , parent_(tree_size + num_symbols_)
    , child_(tree_size)

{
    // Leaves
    for (uint16_t sym = 0; sym < num_symbols_; ++sym) {
        freq_[sym] = 1;
        child_[sym] = sym + tree_size;
        parent_[sym + tree_size] = sym;
    }
    // Internal nodes
    for (uint16_t c = 0, p = num_symbols_; p < tree_size; ++p, c += 2) {
        freq_[p] = freq_[c] + freq_[c + 1];
        child_[p] = c;
        parent_[c] = parent_[c + 1] = p;
    }

    freq_[tree_size] = 0xffff; // sentinel
    parent_[root] = 0;
#ifdef DHUFFTABLE
    build_table();
#endif
}

void DynHuffTree::update(uint16_t sym)
{
    if (freq_[root] == max_freq)
        reconstruct();
    uint16_t node = parent_[sym + tree_size];
    uint16_t max_node = 0;
    do {
        uint16_t node_freq = ++freq_[node];
        uint16_t next = node + 1; // sibling
        if (node_freq > freq_[next]) {
            while (node_freq > freq_[next + 1])
                ++next;

            // Swap nodes
            freq_[node] = freq_[next];
            freq_[next] = node_freq;

            uint16_t node_child = child_[node];
            child_[node] = child_[next];
            child_[next] = node_child;
            set_parent(next);
            set_parent(node);

            node = next;
            max_node = node;
        }
        node = parent_[node];
    } while (node);

#ifdef DHUFFTABLE
    if (max_node > root - 2*(1 << tbits) - 1)
        build_table();
#endif

}

void DynHuffTree::reconstruct()
{
    struct QueueNode {
        uint16_t freq;
        uint16_t val;
        uint16_t order;

        bool operator<(const QueueNode& n) const
        {
            return freq > n.freq || (freq == n.freq && order > n.order);
        }
    };

    std::vector<QueueNode> nodes(num_symbols_ + 1); // +1 to avoid assert for subscript out of range
    uint16_t num_nodes = 0;

    uint16_t order = 0;

    for (uint16_t node = 0; node < tree_size; ++node) {
        const auto ch = child_[node];
        if (ch >= tree_size) {
            auto& n = nodes[num_nodes++];
            n.freq = (freq_[node] + 1) >> 1;
            n.val = ch;
            n.order = order++;
        }
    }
    assert(num_nodes == num_symbols_);
    std::make_heap(&nodes[0], &nodes[num_nodes]);

    uint16_t nodenum = 0;
    auto get_node = [&]() {
        std::pop_heap(&nodes[0], &nodes[num_nodes--]);
        child_[nodenum] = nodes[num_nodes].val;
        freq_[nodenum] = nodes[num_nodes].freq;
        set_parent(nodenum);
        ++nodenum;
        return nodes[num_nodes].freq;
    };
    while (num_nodes >= 2) {
        auto l = get_node();
        auto r = get_node();
        auto& n = nodes[num_nodes++];
        n.freq = l + r;
        n.val = nodenum - 2;
        n.order = order++;
        std::push_heap(&nodes[0], &nodes[num_nodes]);
    }
    get_node();
#ifdef DHUFFTABLE
    build_table();
#endif
}

void DynHuffTree::set_parent(uint16_t node)
{
    const uint16_t child = child_[node];
    if (child >= tree_size)
        parent_[child] = node;
    else
        parent_[child] = parent_[child + 1] = node;
}

uint16_t DynHuffTree::decode(InputBitString& ibs)
{
#ifdef DHUFFTABLE
    const uint16_t table_entry = ibs.peek(tbits);
    uint16_t node = table_[table_entry];
    ibs.drop(table_clen_[table_entry]);
#else
    uint16_t node = child_[root];
#endif
    while (node < tree_size)
        node = child_[node + ibs.get(1)];
    node -= tree_size;
    update(node);
    return node;
}

void DynHuffTree::encode(OutputBitString& obs, uint16_t sym)
{
    assert(sym < num_symbols_);
    uint16_t code = 0;
    uint16_t clen = 0;
    uint16_t node = parent_[sym + tree_size];
    while (node != root) {
        code |= (node & 1) << clen;
        ++clen;
        node = parent_[node];
    }
    obs.put(code, clen);
    update(sym);
}

#ifdef DHUFFTABLE
#include <functional>
void DynHuffTree::build_table()
{
    std::function<void(uint16_t node, uint16_t code, uint8_t len)> fill_table;
    fill_table = [&](uint16_t node, uint16_t code, uint8_t len) {
        const auto ch = child_[node];
        if (len == tbits || ch >= tree_size) {
            code <<= tbits - len;
            for (int i = 0; i < 1 << (tbits - len); ++i, ++code) {
                table_[code] = ch;
                table_clen_[code] = len;
            }
            return;
        }
        code <<= 1;
        ++len;
        fill_table(ch, code, len);
        fill_table(ch + 1, code | 1, len);
    };
    fill_table(root, 0, 0);
    ++nbuilds;
}
#endif

#include <print>
void DynHuffTree::print_info()
{
    std::println("i   ch   fr");
    for (int i = 0; i < tree_size; ++i)
        std::println("{:2d} {:2d} {:2d}", i, child_[i], freq_[i]);
    std::println("i   parent");
    for (int i = 0; i < tree_size + num_symbols_; ++i)
        std::println("{:2d} {:2d}", i, parent_[i]);
    std::println("tree_size {} root {}", tree_size, root);
}

void DynHuffTree::print()
{
    do_print("", root, false);
}

void DynHuffTree::do_print(const char* prefix, int node, bool is_left)
{
    const int ch = child_[node];
    std::print("{}{} ", prefix, is_left ? "|--" : "+--");
    if (ch >= tree_size) {
        std::println("sym {}, freq {}", ch - tree_size, freq_[node]);
        return;
    }
    std::println("node {}, freq {}", node, freq_[node]);
    const auto new_prefix = std::string(prefix) + (is_left ? "|  " : "   ");
    do_print(new_prefix.c_str(), ch, true);
    do_print(new_prefix.c_str(), ch + 1, false);
}

#if 0
std::string DynHuffTree::code_str(uint16_t sym)
{
    assert(sym < num_symbols_);
    uint16_t node = parent_[sym + tree_size];
    std::string res;
    while (node != root) {
        res.insert(res.begin(), (char)('0' + (node & 1)));
        node = parent_[node];
    }
    return res;
}
#endif
