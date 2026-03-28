#ifndef CRC16_H
#define CRC16_H

#include <cstdint>

uint16_t crc16(void* data, size_t size, uint16_t crc = 0);

#endif
