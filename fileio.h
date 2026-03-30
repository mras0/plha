#ifndef FILEIO_H
#define FILEIO_H

#include <vector>
#include <string>
#include <cstdint>
#include <stdexcept>

class FileOpenException : public std::runtime_error {
public:
    explicit FileOpenException(const std::string& msg, int err)
        : std::runtime_error { msg }
        , err_ { err }
    {
    }

    int err() const
    {
        return err_;
    }

    bool err_not_found() const;

private:
    int err_;
};

std::vector<std::uint8_t> read_file(const std::string& filename);
void write_file(const std::string& filename, const void* data, size_t size);
void write_file(const std::string& filename, const std::vector<uint8_t>& data);


int filename_cmp(const char* l, const char* r);

inline bool filenames_equal(const char* l, const char* r)
{
    return filename_cmp(l, r) == 0;
}

struct FilenameCompare {
    using is_transparent = void;

    bool operator()(const std::string& l, const char* r) const
    {
        return filename_cmp(l.c_str(), r) < 0;
    }

    bool operator()(const std::string& l, const std::string& r) const
    {
        return (*this)(l.c_str(), r.c_str());
    }
};

bool wildcard_match(const char* pattern, const char* str);

struct PathComponents {
    std::string dirname;
    std::string filename;
};

PathComponents split_path(const std::string& path);

#endif
