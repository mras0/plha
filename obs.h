#ifndef OBS_H
#define OBS_H

#include <cstdint>
#include <vector>

class OutputBitString {
public:
    explicit OutputBitString() {}

    void put(uint32_t code, uint32_t nb);
    std::vector<uint8_t> finish();

private:
    std::vector<uint8_t> buf_;
    uint32_t bitbuf_ = 0;
    uint32_t bitbuf_cnt_ = 0;
};

#endif
