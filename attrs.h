#ifndef ATTRS_H
#define ATTRS_H

#include <stdint.h>
#include <string>

static constexpr uint32_t PROTECT_MASK_D = 1 << 0; // Delete (not allowed)
static constexpr uint32_t PROTECT_MASK_E = 1 << 1; // Execute (not allowed)
static constexpr uint32_t PROTECT_MASK_W = 1 << 2; // Write (not allowed)
static constexpr uint32_t PROTECT_MASK_R = 1 << 3; // Read (not allowed)
static constexpr uint32_t PROTECT_MASK_A = 1 << 4; // Archived
static constexpr uint32_t PROTECT_MASK_P = 1 << 5; // Pure
static constexpr uint32_t PROTECT_MASK_S = 1 << 6; // Script
static constexpr uint32_t PROTECT_MASK_H = 1 << 7; // Hidden

struct FileAttributes {
    std::string name;
    std::string comment;
    uint32_t protect;
};

std::string protect_string(uint32_t prot);

#ifdef _WIN32
FileAttributes attrs_get(const wchar_t* path);
#else
FileAttributes attrs_get(const char* path);
#endif

#endif
