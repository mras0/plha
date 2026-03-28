#include "fileio.h"
#include <format>
#include <memory>
#include <stdexcept>

struct FileCloser {
    void operator()(FILE * fp)
    {
        if (fp)
            fclose(fp);
    }
};
using FilePointer = std::unique_ptr<FILE, FileCloser>;

FilePointer open_file(const std::string& filename, const char* mode)
{
    FilePointer fp {fopen(filename.c_str(), mode) };
    if (!fp)
        throw std::runtime_error { std::format("Could not open \"{}\" with mode {}", filename, mode) };
    return fp;
}

std::vector<std::uint8_t> read_file(const std::string& filename)
{
    auto fp = open_file(filename, "rb");
    fseek(fp.get(), 0, SEEK_END);
    size_t len = ftell(fp.get());
    std::vector<std::uint8_t> data(len);
    fseek(fp.get(), 0, SEEK_SET);
    if (len) {
        if (auto l = fread(&data[0], 1, len, fp.get()); l != len)
            throw std::runtime_error { std::format("Error reading from \"{}\" {} <> {}", filename, l, len) };
    }
    return data;
}

void write_file(const std::string& filename, const void* data, size_t size)
{
    auto fp = open_file(filename, "wb");
    if (auto sz = fwrite(data, 1, size, fp.get()); sz != size)
        throw std::runtime_error { std::format("Error writing to \"{}\" {} <> {}", filename, sz, size) };
}

void write_file(const std::string& filename, const std::vector<uint8_t>& data)
{
    write_file(filename, data.data(), data.size());
}


