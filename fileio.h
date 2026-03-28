#ifndef FILEIO_H
#define FILEIO_H

#include <vector>
#include <string>
#include <cstdint>

std::vector<std::uint8_t> read_file(const std::string& filename);
void write_file(const std::string& filename, const void* data, size_t size);
void write_file(const std::string& filename, const std::vector<uint8_t>& data);

#endif
