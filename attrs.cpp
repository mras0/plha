#include "attrs.h"
#include "fileio.h"
#include <format>
#include <stdexcept>

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

#ifdef _MSC_VER
#define bswap(x) _byteswap_ulong(x)
#else
#define bswap(x) __builtin_bswap32(x)
#endif

#ifdef _WIN32
#include <memory>
#include <cstring>
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

template<typename CharT, size_t Size>
void set_str(CharT (&dst)[Size], const char* src)
{
    for (size_t i = 0; i < Size; ++i) {
        if (!src[i]) {
            std::memset(&dst[i], 0, sizeof(*dst * (Size - i)));
            return;
        }
        dst[i] = src[i];
    }
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
            s += '_';
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
            e.mode = bswap(e.mode);
            e.windows_mode = bswap(e.windows_mode);
            return true;
        }
    }
    return false;
}

//https://learn.microsoft.com/en-us/windows/win32/sysinfo/converting-a-time-t-value-to-a-file-time
static constexpr int64_t file_time_epoch_offset = 116444736000000000; // 1601-01-01..1970-01-01
static constexpr int64_t file_time_units_per_second = 10000000;
 
static int64_t file_to_time_unix_time(const FILETIME& ft)
{
    return ((ft.dwLowDateTime | (int64_t)ft.dwHighDateTime << 32) - file_time_epoch_offset) / file_time_units_per_second;
}

static void unix_time_to_file_time(FILETIME& ft, int64_t t)
{
    int64_t file_time = t * file_time_units_per_second + file_time_epoch_offset;
    ft.dwLowDateTime = file_time & 0xffffffff;
    ft.dwHighDateTime = file_time >> 32;
}

FileAttributes attrs_get(const wchar_t* path)
{
    WIN32_FILE_ATTRIBUTE_DATA wattrs;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &wattrs))
        throw std::runtime_error { std::format("Could not get file attributes for {:?} - {}", to_str(path), GetLastError()) };

    FileAttributes attrs { };
    if (UaeFsDbEntry entry; read_db_entry(path, entry)) {
        attrs.protect = entry.mode;
        attrs.name = make_str(entry.aname);
        attrs.comment = make_str(entry.comment);
    } else {
        const DWORD attr = wattrs.dwFileAttributes;
        if (attr & FILE_ATTRIBUTE_READONLY)
            attrs.protect |= PROTECT_MASK_D | PROTECT_MASK_W;
        if (attr & FILE_ATTRIBUTE_HIDDEN)
            attrs.protect |= PROTECT_MASK_H;
        if (attr & FILE_ATTRIBUTE_SYSTEM)
            attrs.protect |= PROTECT_MASK_P;
        if (!(attr & FILE_ATTRIBUTE_ARCHIVE))
            attrs.protect |= PROTECT_MASK_A;
    }
    
    FILETIME ftLocal;
    SYSTEMTIME stUtc, stLocal;
    FileTimeToSystemTime(&wattrs.ftLastWriteTime, &stUtc);
    SystemTimeToTzSpecificLocalTime(nullptr, &stUtc, &stLocal);
    SystemTimeToFileTime(&stLocal, &ftLocal);
    attrs.modtime = file_to_time_unix_time(ftLocal);

    return attrs;
}

static bool make_valid_filename(std::string& fname)
{
    //https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file
    bool char_converted = false;
    std::string for_compare;
    for (auto& ch : fname) {
        switch (ch) {
        case '<':
        case '>':
        case ':':
        case '"':
        case '/':
        case '\\':
        case '|':
        case '?':
        case '*':
            ch = '_';
            char_converted = true;
            break;
        }
        for_compare.push_back(ch >= 'a' && ch <= 'z' ? ch & 0xdf : ch);
    }

    bool illegal_filename = false;

    if (for_compare == "CON" || for_compare == "PRN" || for_compare == "AUX" || for_compare == "NUL") {
        illegal_filename = true;
    } else if (for_compare.length() == 4 && (for_compare.compare(0, 3, "COM") == 0 || for_compare.compare(0, 3, "LPT") == 0)) {
        illegal_filename = for_compare[3] >= '1' && for_compare[3] <= '9';
    }

    if (illegal_filename)
        fname = "__uae___" + fname;

    return char_converted || illegal_filename;
}

void write_with_attributes(const std::vector<uint8_t>& data, const std::string& dirname, const FileAttributes& attributes)
{
    // TODO: Dirname with unsupported characters
    // TODO: Handle when two files in the same archive map to the same altered filename..

    auto filename = attributes.name;
    bool need_fsdb = make_valid_filename(filename);
    if (attributes.protect & (PROTECT_MASK_S | PROTECT_MASK_E | PROTECT_MASK_R))
        need_fsdb = true;
    if (auto p = attributes.protect & (PROTECT_MASK_D | PROTECT_MASK_W); p && p != (PROTECT_MASK_D | PROTECT_MASK_W))
        need_fsdb = true;

    const auto full_path = dirname + filename;

    DWORD dwAttr = 0;

    if (auto p = attributes.protect & (PROTECT_MASK_D | PROTECT_MASK_W); p == (PROTECT_MASK_D | PROTECT_MASK_W))
        dwAttr |= FILE_ATTRIBUTE_READONLY;
    if (attributes.protect & PROTECT_MASK_H)
        dwAttr |= FILE_ATTRIBUTE_HIDDEN;
    if (attributes.protect & PROTECT_MASK_P)
        dwAttr |= FILE_ATTRIBUTE_SYSTEM;
    if (!(attributes.protect & PROTECT_MASK_A))
        dwAttr |= FILE_ATTRIBUTE_ARCHIVE;

    // Reset attributes on any existing file
    SetFileAttributesA(full_path.c_str(), FILE_ATTRIBUTE_ARCHIVE);

    write_file(full_path, data);

    if (need_fsdb) {
        const auto stream_name = full_path + ":_UAEFSDB.___";
        handle h { CreateFileA(stream_name.c_str(), FILE_GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, 0, nullptr) };
        if (h.get() == INVALID_HANDLE_VALUE)
            throw std::runtime_error { std::format("Could not create UAE FSDB for {} - {}", full_path, GetLastError()) };

        UaeFsDbEntry e {};
        e.valid = 1;
        e.mode = bswap(attributes.protect);
        set_str(e.aname, attributes.name.c_str());
        set_str(e.w_aname, attributes.name.c_str());
        set_str(e.nname, filename.c_str());
        set_str(e.w_nname, filename.c_str());
        set_str(e.comment, attributes.comment.c_str());
        e.windows_mode = bswap(dwAttr);

        DWORD nwrite;
        if (!WriteFile(h.get(), &e, sizeof(e), &nwrite, nullptr) || nwrite != sizeof(e))
            throw std::runtime_error { std::format("Could not write UAEFSDB entry for {:?} - nread {}", full_path, nwrite) };
    }

    // Set modified date/time
    handle h { CreateFileA(full_path.c_str(), FILE_GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr) };
    FILETIME ftLocal, ftUtc;
    SYSTEMTIME stUtc, stLocal;
    unix_time_to_file_time(ftLocal, attributes.modtime);
    FileTimeToSystemTime(&ftLocal, &stLocal);
    TzSpecificLocalTimeToSystemTime(nullptr, &stLocal, &stUtc);
    SystemTimeToFileTime(&stUtc, &ftUtc);

    if (h.get() == INVALID_HANDLE_VALUE || !SetFileTime(h.get(), &ftUtc, &ftUtc, &ftUtc))
        throw std::runtime_error { std::format("Could not modify filetime for {} - {}", full_path, GetLastError()) };
    h.release();

    // Modify attributes last (otherwise can't set date/time)
    if (!need_fsdb && !SetFileAttributesA(full_path.c_str(), dwAttr))
        throw std::runtime_error { std::format("Could not set attributes for {} - {}", full_path, GetLastError()) };
}

#else

#include <sys/stat.h>
#include <errno.h>

// TODO
FileAttributes attrs_get(const char* path)
{
    FileAttributes attrs {};

    struct stat st;
    if (stat(path, &st))
        throw std::runtime_error { std::format("Could not set attributes for {} - {}", path, errno) };
    attrs.modtime = st.st_mtime;
    return attrs;
}

void write_with_attributes(const std::vector<uint8_t>& data, const std::string& dirname, const FileAttributes& attributes)
{
    write_file(dirname + attributes.name, data);
}

#endif // _WIN32
