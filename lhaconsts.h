#ifndef LHACONSTS_H
#define LHACONSTS_H

static constexpr uint32_t max_match = 256;
static constexpr uint32_t treshold = 3;
static constexpr uint32_t NT = 16 + 3; // USHRT_BIT + 3
static constexpr uint32_t NC = 255 + max_match + 2 - treshold;
static constexpr uint32_t TBIT = 5;
static constexpr uint32_t CBIT = 9;

#endif
