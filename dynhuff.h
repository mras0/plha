#ifndef DYNHUFF_H
#define DYNHUFF_H

#include <cstdint>
#include <vector>

class InputBitString;
class OutputBitString;

class DynHuffTree {
public:
    explicit DynHuffTree(uint16_t num_symbols);

    uint16_t decode(InputBitString& ibs);
    void encode(OutputBitString& obs, uint16_t sym);

private:
    void update(uint16_t sym);
    void reconstruct();
    void set_parent(uint16_t node);

    const uint16_t num_symbols_;
    const uint16_t tree_size = num_symbols_ * 2 - 1;
    const uint16_t root = tree_size - 1;
    static constexpr uint16_t max_freq = 0x8000;

    std::vector<uint16_t> freq_; // freq[i] <= freq[i+1]
    std::vector<uint16_t> parent_; // tree_size..tree_size+num_symbols-1 contain the starting node for a symbol
    std::vector<uint16_t> child_;
};



#endif
