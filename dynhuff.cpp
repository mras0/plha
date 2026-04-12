#include "dynhuff.h"
#include "ibs.h"
#include <queue>

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
}

void DynHuffTree::update(uint16_t sym)
{
    if (freq_[root] == max_freq)
        reconstruct();
    uint16_t node = parent_[sym + tree_size];
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
        }
        node = parent_[node];
    } while (node);
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

    std::priority_queue<QueueNode> pq;
    uint16_t order = 0;

    for (uint16_t node = 0; node < tree_size; ++node) {
        const auto ch = child_[node];
        if (ch >= tree_size) {
            QueueNode n;
            n.freq = (freq_[node] + 1) >> 1;
            n.val = ch;
            n.order = order++;
            pq.push(n);
        }
    }
         
    uint16_t nodenum = 0;
    auto get_node = [&]() {
        auto n = pq.top();
        pq.pop();
        child_[nodenum] = n.val;
        freq_[nodenum] = n.freq;
        set_parent(nodenum);
        ++nodenum;
        return n.freq;
    };
    for (; pq.size() >= 2;) {
        auto l = get_node();
        auto r = get_node();
        QueueNode n;
        n.freq = l + r;
        n.val = nodenum - 2;
        n.order = order++;
        pq.push(n);
    }
    get_node();

#if 0
    // Collect leaves in first half and halve frequency
    for (uint16_t node = 0, nsyms = 0; node < tree_size; ++node) {
        if (child_[node] < tree_size)
            continue;
        freq_[nsyms] = (freq_[node] + 1) >> 1;
        child_[nsyms] = child_[node];
        ++nsyms;
    }
    // Reconstruct tree
    for (uint16_t c = 0, p = num_symbols; p < tree_size; ++p, c += 2) {
        const uint16_t f = freq_[p] = freq_[c] + freq_[c + 1];

        // Find where in the tree this node should go
        uint16_t k;
        for (k = p - 1; f < freq_[k]; --k)
            ;
        k++;

        #if 1
        for (uint16_t i = p; --i >= k;) {
            freq_[i + 1] = freq_[i];
            child_[i + 1] = child_[i];
        }
        freq_[k] = f;
        child_[k] = c;
        #else
        uint16_t l = (p - k) * sizeof(uint16_t);
        memmove(&freq_[k + 1], &freq_[k], l);
        freq_[k] = f;
        memmove(&child_[k + 1], &child_[k], l);
        child_[k] = c;
        #endif
    }
    for (int i = 0; i < tree_size; ++i) {
        if (nodes[i].val != child_[i] || nodes[i].freq != freq_[i]) {
            std::println("{:3x} {:3x} {:4d} --- {:3x} {:4d}", i, nodes[i].val, nodes[i].freq, child_[i], freq_[i]);
            exit(1);
        }
    }

    // Reconnect parent
    for (uint16_t i = 0; i < tree_size; ++i)
        set_parent(i);
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
    uint16_t node = child_[root];
    while (node < tree_size)
        node = child_[node + ibs.get(1)];
    node -= tree_size;
    update(node);
    return node;
}

#if 0
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

void DynHuffTree::do_print(const std::string& prefix, int node, bool is_left, int level = 0)
{
    const int ch = child_[node];
    std::print("{}{} ", prefix, is_left ? "|--" : "+--");
    if (ch >= tree_size) {
        std::println("sym {}, freq {}", ch - tree_size, freq_[node]);
        return;
    }
    std::println("node {}, freq {}", node, freq_[node]);
    const auto new_prefix = prefix + (is_left ? "|  " : "   ");
    do_print(new_prefix, ch, true, level + 1);
    do_print(new_prefix, ch + 1, false, level + 1);
}

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
