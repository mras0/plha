#include "ibs.h"
#include <stdexcept>

void InputBitString::fill()
{
    while (bufcnt_ < 16) {
        bitbuf_ <<= 8;
        if (data_ < end_)
            bitbuf_ |= *data_++;
        else if (++data_ == end_ + 3)
            throw std::runtime_error { "Input overrun" };
        bufcnt_ += 8;
    }
}
