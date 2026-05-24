#include "attrs.h"
#include "util.h"

std::string protect_string(uint32_t prot)
{
    prot ^= 0xf; // rwed are stored inverted (0 => can be deleted for example)
    std::string res;
    for (int i = 0; i < 8; ++i) {
        res += (prot & 0x80) ? "hsparwed"[i] : '-';
        prot <<= 1;
    }
    return res;
}

#ifdef _WIN32
#include <format>
#include <stdexcept>
#include <memory>
#include <Windows.h>

#pragma pack(push, 1)
struct UaeFsDbEntry {
    uint8_t valid;
    uint32_t mode;
    char aname[257];
    char nname[257];
    char comment[81];
    uint32_t windows_mode;
    // 1.6.0+
    wchar_t w_aname[257];
    wchar_t w_nname[257];
};
static_assert(sizeof(UaeFsDbEntry) == 1632);
#pragma pack(pop)

template<size_t Size>
static std::string make_str(const char (&arr)[Size])
{    
    for (size_t l = Size; l--;) {
        if (arr[l])
            return std::string(arr, arr + l + 1);
    }
    return "";
}

struct HandleCloser {
    void operator()(HANDLE h) const
    {
        if (h && h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
};
using handle = std::unique_ptr<void, HandleCloser>;

static std::string to_str(const wchar_t* path)
{
    std::string s;
    while (*path) {
        const auto ch = *path++;
        if (ch < 0x7f)
            s += static_cast<char>(ch);
        else
            s += '?';
    }
    return s;
}

static bool read_db_entry(const wchar_t* path, UaeFsDbEntry& e)
{
    const auto stream_name = std::wstring(path) + L":_UAEFSDB.___";
    handle h { CreateFileW(stream_name.c_str(), FILE_GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr) };
    if (h.get() != INVALID_HANDLE_VALUE) {
        if (const auto size = GetFileSize(h.get(), nullptr); size != sizeof(UaeFsDbEntry))
            throw std::runtime_error { std::format("Invalid UAEFSDB entry for {:?} - size {}", to_str(path), size) };
        DWORD nread;
        if (!ReadFile(h.get(), &e, sizeof(e), &nread, nullptr) || nread != sizeof(e))
            throw std::runtime_error { std::format("Could not read UAEFSDB entry for {:?} - nread {}", to_str(path), nread) };
        if (e.valid) {
            e.mode = _byteswap_ulong(e.mode);
            e.windows_mode = _byteswap_ulong(e.windows_mode);
            return true;
        }
    }
    return false;
}

FileAttributes attrs_get(const wchar_t* path)
{
    FileAttributes attrs { };
    if (UaeFsDbEntry entry; read_db_entry(path, entry)) {
        attrs.protect = entry.mode;
        attrs.name = make_str(entry.aname);
        attrs.comment = make_str(entry.comment);
    } else {
        const DWORD attr = GetFileAttributesW(path);
        if (attr == INVALID_FILE_ATTRIBUTES)
            throw std::runtime_error { std::format("Could not get file attributes for {:?} - {}", to_str(path), GetLastError()) };

        if (attr & FILE_ATTRIBUTE_READONLY)
            attrs.protect |= PROTECT_MASK_D | PROTECT_MASK_W;
        if (attr & FILE_ATTRIBUTE_HIDDEN)
            attrs.protect |= PROTECT_MASK_H;
        if (attr & FILE_ATTRIBUTE_SYSTEM)
            attrs.protect |= PROTECT_MASK_P;
        if (!(attr & FILE_ATTRIBUTE_ARCHIVE))
            attrs.protect |= PROTECT_MASK_A;
    }

    return attrs;
}
#else
FileAttributes attrs_get(const char* path)
{
    // TODO
    FileAttributes attrs {};
    attrs.name = path;
    return attrs;
}
#endif // _WIN32
