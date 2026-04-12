#ifndef HUFFTABLE_H
#define HUFFTABLE_H

#include <cstdint>
#include <vector>

class InputBitString;

class HuffTable {
public:
    explicit HuffTable(uint16_t num_syms, uint32_t table_bits);

    void set_lengths(const uint8_t* len);
    void read_pt_len(InputBitString& ibs, uint32_t nbit, uint32_t special = UINT32_MAX);
    void read_c_len(InputBitString& ibs, const HuffTable& tab);
    uint16_t decode(InputBitString& ibs) const;

private:
    static constexpr uint32_t max_table_size = 4096;
    const uint16_t num_syms_;
    const uint32_t table_bits_;
    const uint32_t tree_size_;
    std::vector<uint8_t> code_len_;
    std::vector<uint16_t> table_;
    std::vector<uint16_t> left_;
    std::vector<uint16_t> right_;

    void make_table();
    void empty_table(uint16_t sym);
};

#endif
