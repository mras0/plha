#ifndef IBS_H
#define IBS_H

#include <cstdint>
#include <cassert>

class InputBitString {
public:
    explicit InputBitString(const uint8_t* data, uint32_t size)
        : data_ { data }
        , end_ { data + size }
    {
        fill();
    }

    void drop(uint32_t cnt)
    {
        assert(cnt <= bufcnt_);
        bufcnt_ -= cnt;
        bitbuf_ &= (1 << bufcnt_) - 1;
        fill();
    }

    uint16_t peek(uint32_t cnt)
    {
        assert(cnt <= bufcnt_);
        return static_cast<uint16_t>(bitbuf_ >> (bufcnt_ - cnt));
    }

    uint16_t get(uint32_t cnt)
    {
        auto res = peek(cnt);
        drop(cnt);
        return res;
    }

private:
    const uint8_t* data_;
    const uint8_t* const end_;
    uint32_t bitbuf_ = 0;
    uint32_t bufcnt_ = 0;

    void fill();
};


#endif
