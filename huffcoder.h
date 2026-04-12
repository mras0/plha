#ifndef HUFFCODER_H
#define HUFFCODER_H

#include <vector>
#include <cstdint>
#include "obs.h"

class HuffCoder {
public:
    explicit HuffCoder(const std::vector<uint32_t>& freq);
    explicit HuffCoder(const uint8_t* codelen, uint32_t num_syms);

    void encode(OutputBitString& obs, uint16_t sym) const;

    void encode_table_c(OutputBitString& obs) const;
    void encode_table_p(OutputBitString& obs, uint32_t window_bits) const;

    const std::vector<uint32_t>& code_length() const
    {
        return clen_;
    }

private:
    const uint32_t num_sym_;
    std::vector<uint32_t> clen_;
    std::vector<uint32_t> code_;
};

#endif
