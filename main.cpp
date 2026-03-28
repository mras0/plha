#include <print>
#include <string>
#include <stdexcept>
#include <vector>
#include <memory>
#include <cstdint>
#include <cassert>
#include <algorithm>
#include "fileio.h"
#include "lhafile.h"
#include "util.h"
#include "decompress.h"

void test_file(const std::string& filename)
{
    std::println("{}", filename);
    auto data = read_file(filename);
    LhaFileReader lha { data.data(), data.size() };
    for (LhaHeader hdr; lha.next(hdr);) {
        // Note: The filename can be NUL-terminated and contain version info afterwards
        std::println("{:5.5s} {:6d} {:6d} {}{}", (const char*)hdr.compression_method, hdr.compressed_size, hdr.original_size, hdr.dirname, hdr.filename);
        if (!memcmp(hdr.compression_method, "-lhd-", 5))
            continue;
        (void)decompress(data, hdr);
    }
}

#include <filesystem>
void test_dir(const std::string& dir_path)
{
    namespace fs = std::filesystem;
    for (const auto& e : fs::recursive_directory_iterator { dir_path }) {
        try {
            if (e.is_directory())
                continue;
            const auto path = e.path();
            if (path.extension() != ".lha")
                continue;
            if (path.filename() == "im-tools.lha")
                continue;
            if (path.filename().u8string().starts_with(u8"bgui"))
                continue;
        } catch (const std::exception& ex) {
            std::println("{} - {}", e.path().string(), ex.what());
            continue;
        }

        test_file(e.path().string());
    }
}

// -lh5-  21493 170782 80croc.def
// -lh5-  42462 112384 BLOX1.DAT
// -lh5-    733   3475 Green Eggs and Ham.txt
// -lh5-  68775 118784 MAIN.BIN
// -lh5-  57545 245720 Zombies.SHP
// -lh5- 518758 570307 data.adpcm
// -lh5- 127188 267264 jp2_000
// -lh5-  17421  34468 jp2_001
// -lh5-  21751  51100 jp2_002
// -lh5-     20     96 simple.txt
// -lh5-   3378  14984 sprite_intro

int main()
{
    try {
        const char* const tests[] = {
            R"(c:\temp\os-source\aug.cats\av\update_src\av.lha)", // 0 byte padded
            //R"(c:\Users\micha\Downloads\im-tools.lha)", // no CRC (?)
            R"(c:\Users\micha\Downloads\DylanDog_Complete_WHD.lha)", // lh0/lh5
            R"(c:\Users\micha\Downloads\WHDLoad_dev.lha)",// lh0/lh5
            R"(c:\Temp\whdload_test\Kefrens-AnkhInPopland\source\Install\Kefrens-AnkhInPopland.lha)",
            R"(c:\Users\micha\Downloads\tg93mods.lha)", // Header level 0
            R"(c:\Users\micha\Downloads\3dstars.lha)", // Header level 0 with directory
            R"(explode.lha)",
            R"(spaces.lha)",
            R"(c:\Users\micha\Downloads\SDK_54.16.lha)", // lh0/lh6
            R"(c:\Users\micha\Downloads\elk-knarkzilla.lha)", // Empty table in c_len     
            R"(c:\Users\micha\Downloads\gcc68.lha)", // lhz7/header level 2, match position > outpos
            R"(test.lha)",
        };

        //test_dir(R"(c:\Users\micha\Downloads\)");
        //test_dir(R"(c:\temp\)");
        //test_dir(R"(c:\prog\amiga\)");
        //test_dir(R"(c:\prog)");
        //test_dir(R"(c:\tools)");
        //exit(0);

        for (const auto& fn : tests)
            test_file(fn);

    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
