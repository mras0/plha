#include "obs.h"
#include <cassert>

void OutputBitString::put(uint32_t code, uint32_t nb) {
    assert(nb <= 16);
    assert(bitbuf_cnt_ <= 16);
    assert(code >> nb == 0);
    bitbuf_ = bitbuf_ << nb | code;
    bitbuf_cnt_ += nb;
    while (bitbuf_cnt_ >= 8) {
        bitbuf_cnt_ -= 8;
        buf_.push_back(uint8_t(bitbuf_ >> bitbuf_cnt_));
    }
}

std::vector<uint8_t> OutputBitString::finish()
{
    while (bitbuf_cnt_)
        put(0, 1);
    return std::move(buf_);
}
