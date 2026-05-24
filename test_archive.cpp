#include <print>
#include "lhafile.h"
#include "fileio.h"
#include "decompress.h"
#include "lhacompress.h"

#define CHECK_EQ(l, r) do { auto _l = l; auto _r = r; if (_l != _r) throw std::runtime_error{std::format("({} == {}) failed: {} != {}", #l, #r, _l, _r)}; } while (0)

void test_filename_cmp()
{
    CHECK_EQ(filename_cmp("Aa", "Aa"), 0);
    CHECK_EQ(filename_cmp("Aa", "AB"), -1);
    CHECK_EQ(filename_cmp("aC", "AB"), 1);
}

void test_wildcard_match()
{
    CHECK_EQ(wildcard_match("test", "TesT"), true);
    CHECK_EQ(wildcard_match("test", "TesT0"), false);
    CHECK_EQ(wildcard_match("?a", "Aa"), true);
    CHECK_EQ(wildcard_match("?a", "ab"), false);
    CHECK_EQ(wildcard_match("a*", "a"), true);
    CHECK_EQ(wildcard_match("a*", "ab"), true);
    CHECK_EQ(wildcard_match("*a*", "ab"), true);
    CHECK_EQ(wildcard_match("m*issip*", "mississippi"), true);
    CHECK_EQ(wildcard_match("da*da*da*", "daaadabadmanda"), true);
    CHECK_EQ(wildcard_match("/foo/*/bar", "/foo/long/path/..../bar"), true);
    CHECK_EQ(wildcard_match("/foo/*/bar", "/foo/long/bar/..../bar"), true);
    CHECK_EQ(wildcard_match("/foo/*/bar", "/foo/long/bar/..../barx"), false);
    CHECK_EQ(wildcard_match("*.txt", "test.txt"), true);
    CHECK_EQ(wildcard_match("*.txt", "test.123.txt"), true);
    CHECK_EQ(wildcard_match("*.txt", "test.123.xt"), false);
}

bool operator==(const PathComponents& l, const PathComponents& r)
{
    return l.dirname == r.dirname && l.filename == r.filename;
}

template <>
struct std::formatter<PathComponents> : std::formatter<const char*> {
    auto format(const PathComponents& pc, std::format_context& ctx) const
    {
        return std::formatter<const char*>::format(std::format("[{:?}, {:?}]", pc.dirname, pc.filename).c_str(), ctx);
    }
};


void test_split_path()
{
    using PC = PathComponents;
    CHECK_EQ(PC ( "/foo/", "bar" ), split_path("/foo/bar"));
    CHECK_EQ(PC("../foo/bar/", "boo"), split_path("../foo/bar/boo"));
    CHECK_EQ(PC("", "test.txt"), split_path("test.txt"));
    CHECK_EQ(PC("dir/name/", "blah"), split_path("dir\\name\\blah"));
}

void test_max_ratio_percent()
{
    std::vector<uint8_t> arc_data;
    LhaCompressOptions options {};
    options.method = LHA_METHOD_LH6;
    std::vector<uint8_t> orig_data;
    for (int i = 0; i < 256; ++i)
        orig_data.push_back(static_cast<uint8_t>(i));
    lha_compress(arc_data, orig_data, "", "foo", 1234, 0, options);
    LhaFileReader reader { arc_data.data(), arc_data.size() };
    LhaHeader rhdr;
    CHECK_EQ(reader.next(rhdr), true);
    CHECK_EQ((int)lha_method_from_id(rhdr.compression_method), (int)LHA_METHOD_LH0);
}

int main()
{
    try {
        test_filename_cmp();
        test_wildcard_match();
        test_split_path();
        test_max_ratio_percent();

        std::vector<uint8_t> arc_data;
        const std::string fname = "main.cpp";
        const std::string dirname = "foo/bar/";
        auto orig_data = read_file("../" + fname);
        const auto method = LHA_METHOD_LH5;
        const uint32_t modtime = 1774864640;
        const uint32_t protect = 0xf7;
        LhaCompressOptions options {};
        options.method = method;
        lha_compress(arc_data, orig_data, dirname, fname, modtime, protect, options);

        LhaFileReader reader { arc_data.data(), arc_data.size() };
        LhaHeader rhdr;
        CHECK_EQ(reader.next(rhdr), true);
        CHECK_EQ(rhdr.filename, fname);
        CHECK_EQ(rhdr.dirname, dirname);
        CHECK_EQ(rhdr.original_size, orig_data.size());
        CHECK_EQ(rhdr.os, lha_os_amiga);
        CHECK_EQ(rhdr.protect, protect);
        CHECK_EQ((int)lha_method_from_id(rhdr.compression_method), (int)method);

        auto decompressed = decompress(arc_data, rhdr);
        CHECK_EQ(decompressed, orig_data);

        CHECK_EQ(reader.next(rhdr), false);

        //write_file("test.lha", arc_data);

    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
    return 0;
}
