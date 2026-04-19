#include <print>
#include <string>
#include <stdexcept>
#include <vector>
#include "fileio.h"
#include "lhafile.h"
#include "decompress.h"

void test_file(const std::string& filename)
{
    std::println("{}", filename);
    auto data = read_file(filename);
    LhaFileReader lha { data.data(), data.size() };
    for (LhaHeader hdr; lha.next(hdr);) {
        // Note: The filename can be NUL-terminated and contain version info afterwards
        std::println("{:5.5s} {:6d} {:6d} {}{}", (const char*)hdr.compression_method, hdr.compressed_size, hdr.original_size, hdr.dirname, hdr.filename);
        if (lha_method_from_id(hdr.compression_method) == LHA_METHOD_DIR)
            continue;
        (void)decompress(data, hdr);
    }
}

#include <filesystem>

static std::vector<std::pair<std::string, std::string>> failed;
void test_dir(const std::string& dir_path)
{
    namespace fs = std::filesystem;
    for (const auto& e : fs::recursive_directory_iterator { dir_path, fs::directory_options::skip_permission_denied }) {
        try {
            if (e.is_directory())
                continue;
            const auto path = e.path();
            if (/*path.extension() != ".lha" && */path.extension() != ".lzh")
                continue;
            if (path.filename() == "im-tools.lha") // No CRC?
                continue;
            if (path.filename().u8string().starts_with(u8"bgui")) // AROS fake .lha's
                continue;
        } catch (const std::exception& ex) {
            std::println("{} - {}", e.path().string(), ex.what());
            continue;
        }
        try {
            test_file(e.path().string());
        } catch (const std::exception& ex) {
            std::println("{} - {}", e.path().string(), ex.what());
            failed.push_back({ e.path().string(), ex.what() });
        }
    }
}

int main()
{
    //blkstuff.lzh - Not valid
    //TODO: lha\tests\lha-test16-l0.lzh - Invalid header size. Extended header size -12
    try {
        //test_dir("c:/");
        test_file("../test_decomp/glowria.lzh");
        test_file("../test_decomp/PPDecrunch10.lzh");
        test_dir("../test_decomp");

        for (const auto& [f, e] : failed)
            std::println("{} - {}", f, e);

        if (!failed.empty())
            return 1;

    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
