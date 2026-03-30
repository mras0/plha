#include "fileio.h"
#include <format>
#include <memory>
#include <stdexcept>
#include <cerrno>
#include <cstring>
#include <algorithm>

struct FileCloser {
    void operator()(FILE * fp)
    {
        if (fp)
            fclose(fp);
    }
};
using FilePointer = std::unique_ptr<FILE, FileCloser>;

bool FileOpenException::err_not_found() const
{
    return err() == ENOENT;
}

FilePointer open_file(const std::string& filename, const char* mode)
{
    FilePointer fp {fopen(filename.c_str(), mode) };
    if (!fp)
        throw FileOpenException { std::format("Could not open \"{}\" with mode {} - {}", filename, mode, std::strerror(errno)), errno };
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

static uint8_t to_upper(uint8_t ch)
{
    return ch >= 'a' && ch <= 'z' ? ch + 'A' - 'a' : ch;
}

int filename_cmp(const char* l, const char* r)
{
    for (;;) {
        const auto lc = static_cast<uint8_t>(*l++);
        const auto rc = static_cast<uint8_t>(*r++);
        if (!lc && !rc)
            return 0;
        if (lc == rc)
            continue;
        const int diff = to_upper(lc) - to_upper(rc);
        if (diff)
            return diff < 0 ? -1 : 1;
    }
}

bool wildcard_match(const char* pattern, const char* str)
{
    for (;;) {
        const char p = *pattern++;
        const char s = *str;
        if (!p)
            return !*str;
        if (p == '?') {
            if (!s)
                return false;
        } else if (p == '*') {
            for (auto s2 = str; *s2; ++s2)
                if (wildcard_match(pattern, s2))
                    return true;
            return !*pattern;
        } else if (to_upper(p) != to_upper(s))
            return false;
        ++str;
    }
}

PathComponents split_path(const std::string& path)
{
    auto sep_pos = path.find_last_of("/\\");
    if (sep_pos == std::string::npos)
        return { "", path };
    ++sep_pos;
    
    PathComponents pc;
    pc.dirname = path.substr(0, sep_pos);
    pc.filename = path.substr(sep_pos);
    std::replace(pc.dirname.begin(), pc.dirname.end(), '\\', '/');
    return pc;
}