#ifndef DYNHUFF_H
#define DYNHUFF_H

#include <cstdint>
#include <vector>

class InputBitString;
class OutputBitString;

//#define DHUFFTABLE // Need reconstruction too often...

class DynHuffTree {
public:
    explicit DynHuffTree(uint16_t num_symbols);

    uint16_t decode(InputBitString& ibs);
    void encode(OutputBitString& obs, uint16_t sym);

    void print_info();
    void print();

private:
#ifdef DHUFFTABLE
    static constexpr uint16_t tbits = 3;
    static constexpr uint16_t table_size = 1 << tbits;
    uint16_t table_[table_size];
    uint8_t table_clen_[table_size];
    void build_table();
#endif

    const uint16_t num_symbols_;
    const uint16_t tree_size = num_symbols_ * 2 - 1;
    const uint16_t root = tree_size - 1;
    static constexpr uint16_t max_freq = 0x8000;

    std::vector<uint16_t> freq_; // freq[i] <= freq[i+1]
    std::vector<uint16_t> parent_; // tree_size..tree_size+num_symbols-1 contain the starting node for a symbol
    std::vector<uint16_t> child_;

    void update(uint16_t sym);
    void reconstruct();
    void set_parent(uint16_t node);

    void do_print(const char* prefix, int node, bool is_left);
};



#endif
