#include <print>
#include <stdexcept>
#include <vector>
#include <cassert>
#include <cstring>
#include "fileio.h"
#include "util.h"
#include "lhaconsts.h"
#include "obs.h"
#include "lz.h"
#include "compress.h"

void test_lz(const uint8_t* data, uint32_t size, const std::vector<LzNode>& lz)
{
    std::vector<uint8_t> out;

    for (const auto& n : lz) {
        if (n.code < 256) {
            out.push_back((uint8_t)n.code);
            continue;
        }
        uint32_t len = n.code - 256 + min_match_len;
        uint32_t ofs = n.ofs + 1;
        if (ofs > out.size())
            throw std::runtime_error {std::format("Invalid offset in LZ stream {} (max {})", ofs, out.size()) };
        while (len--)
            out.push_back(out[out.size() - ofs]);
    }

    if (out.size() != size)
        throw std::runtime_error { std::format("Decompression failed size: {} <> {}", out.size(), size) };
    if (!size || !std::memcmp(data, out.data(), size))
        return;

    std::println("LZ Decompression failed!");

    for (size_t i = 0; i < size; ++i) {
        if (out[i] == data[i])
            continue;
        std::println("First difference at {}", i);
        const auto sz = std::min(size_t(16), size - i);
        std::println("Expected:");
        hexdump(data + i, sz);
        std::println("Got:");
        hexdump(&out[i], sz);
        break;
    }

    exit(1);
}

#include "decompress.h"

void test_file(const std::string& filename, const std::vector<uint8_t>& data, LhaMethod method)
{
    const auto lz = lz_build(data.data(), (uint32_t)data.size(), window_bits_for_method(method));
    test_lz(data.data(), (uint32_t)data.size(), lz);
    const auto encoded = compress(lz, method);
    const auto decoded = decompress(encoded.data(), (uint32_t)encoded.size(), (uint32_t)data.size(), method);

    if (decoded != data)
        throw std::runtime_error { std::format("Wrong data decoded for {}", filename) };

    std::println("{:30s} {:6d} {:6d} {:.2f}% {:5.5s}", filename, encoded.size(), decoded.size(), 100. * encoded.size() / decoded.size(), (const char*)lha_method_names[method]);
}

void test_file(const std::string& filename, LhaMethod method)
{
    test_file(filename, read_file(filename), method);
}

#include <filesystem>
namespace fs = std::filesystem;
void test_dir(const std::string& dir_path, LhaMethod method)
{
    for (const auto& e : fs::recursive_directory_iterator { dir_path })
        test_file(e.path().string(), method);
}

#include "lhafile.h"
void test_recompress()
{
    for (const auto& e : fs::recursive_directory_iterator { "../test_decomp" }) {
        if (e.path().filename() == "im-tools.lha")
            continue;
        const auto lha_file = read_file(e.path().string());
        LhaFileReader lf {lha_file.data(), lha_file.size()};
        size_t orig_compressed = 0, new_compressed = 0;
        for (LhaHeader hdr; lf.next(hdr);) {
            const auto method = lha_method_from_id(hdr.compression_method);
            if (method == LHA_METHOD_DIR || method == LHA_METHOD_LH0)
                continue;
            orig_compressed += hdr.compressed_size;
            const auto orig_data = decompress(lha_file, hdr);
            const auto new_comp = compress(orig_data.data(), (uint32_t)orig_data.size(), method);
            std::println("{}{} {} {}", hdr.dirname, hdr.filename, hdr.compressed_size, new_comp.size());
            if (decompress(new_comp.data(), (uint32_t)new_comp.size(), (uint32_t)orig_data.size(), method) != orig_data)
                throw std::runtime_error { std::format("Decompression failed for {}{}", hdr.dirname, hdr.filename) };
            new_compressed += new_comp.size();
        }

        std::println("{:30s} {:6d} {:6d} {:.2f}%", e.path().string(), new_compressed, orig_compressed, new_compressed * 100. / orig_compressed);

    }
}


#include "ibs.h"
void test_obs()
{
    OutputBitString obs {};
    obs.put(0xABCD, 16);
    obs.put(42, 7);
    obs.put(123, 8);
    const auto res = obs.finish();
    InputBitString ibs { res.data(), (uint32_t)res.size() };
    assert(ibs.get(16) == 0xABCD);
    assert(ibs.get(7) == 42);
    assert(ibs.get(8) == 123);
}

/*
Unix LHA:
-lh5-  21493 170782 80croc.def
-lh5-  42462 112384 BLOX1.DAT
-lh5-    733   3475 Green Eggs and Ham.txt
-lh5-  68775 118784 MAIN.BIN
-lh5-  57545 245720 Zombies.SHP
-lh5- 518758 570307 data.adpcm
-lh5- 127188 267264 jp2_000
-lh5-  17421  34468 jp2_001
-lh5-  21751  51100 jp2_002
-lh5-     20     96 simple.txt
-lh5-   3378  14984 sprite_intro

First working version (max search = 4):
../test_comp/80croc.def         30693 170782 17.97%
../test_comp/BLOX1.DAT          44650 112384 39.73%
../test_comp/data.adpcm        518496 570307 90.92%
../test_comp/Green Eggs and Ham.txt    798   3475 22.96%
../test_comp/jp2_000           133060 267264 49.79%
../test_comp/jp2_001            17859  34468 51.81%
../test_comp/jp2_002            22432  51100 43.90%
../test_comp/MAIN.BIN           71953 118784 60.57%
../test_comp/simple.txt            20     96 20.83%
../test_comp/sprite_intro        3505  14984 23.39%
../test_comp/Zombies.SHP        62545 245720 25.45%

Max search 64:
../test_comp/80croc.def         23993 170782 14.05%
../test_comp/BLOX1.DAT          43772 112384 38.95%
../test_comp/data.adpcm        518506 570307 90.92%
../test_comp/Green Eggs and Ham.txt    753   3475 21.67%
../test_comp/jp2_000           130102 267264 48.68%
../test_comp/jp2_001            17628  34468 51.14%
../test_comp/jp2_002            22086  51100 43.22%
../test_comp/MAIN.BIN           71215 118784 59.95%
../test_comp/simple.txt            20     96 20.83%
../test_comp/sprite_intro        3400  14984 22.69%
../test_comp/Zombies.SHP        61097 245720 24.86%

Lookahead 1:
../test_comp/80croc.def         23147 170782 13.55%
../test_comp/BLOX1.DAT          42967 112384 38.23%
../test_comp/data.adpcm        518506 570307 90.92%
../test_comp/Green Eggs and Ham.txt    734   3475 21.12%
../test_comp/jp2_000           128596 267264 48.12%
../test_comp/jp2_001            17491  34468 50.75%
../test_comp/jp2_002            21861  51100 42.78%
../test_comp/MAIN.BIN           70663 118784 59.49%
../test_comp/simple.txt            20     96 20.83%
../test_comp/sprite_intro        3386  14984 22.60%
../test_comp/Zombies.SHP        59285 245720 24.13%

"Optimal" max match=16
../test_comp/80croc.def         22595 170782 13.23%
../test_comp/BLOX1.DAT          42098 112384 37.46%
../test_comp/data.adpcm        518506 570307 90.92%
../test_comp/Green Eggs and Ham.txt    721   3475 20.75%
../test_comp/jp2_000           127210 267264 47.60%
../test_comp/jp2_001            17197  34468 49.89%
../test_comp/jp2_002            21530  51100 42.13%
../test_comp/MAIN.BIN           69852 118784 58.81%
../test_comp/simple.txt            20     96 20.83%
../test_comp/sprite_intro        3353  14984 22.38%
../test_comp/Zombies.SHP        57469 245720 23.39%

max match=64
../test_comp/80croc.def         21783 170782 12.75%
../test_comp/BLOX1.DAT          41933 112384 37.31%
../test_comp/data.adpcm        518506 570307 90.92%
../test_comp/Green Eggs and Ham.txt    716   3475 20.60%
../test_comp/jp2_000           126842 267264 47.46%
../test_comp/jp2_001            17158  34468 49.78%
../test_comp/jp2_002            21461  51100 42.00%
../test_comp/MAIN.BIN           69728 118784 58.70%
../test_comp/simple.txt            20     96 20.83%
../test_comp/sprite_intro        3344  14984 22.32%
../test_comp/Zombies.SHP        57252 245720 23.30%

cheaper cost for 00/FF
../test_comp/80croc.def         21770 170782 12.75%
../test_comp/BLOX1.DAT          41641 112384 37.05%
../test_comp/data.adpcm        518493 570307 90.91%
../test_comp/Green Eggs and Ham.txt    716   3475 20.60%
../test_comp/jp2_000           126670 267264 47.40%
../test_comp/jp2_001            17038  34468 49.43%
../test_comp/jp2_002            21298  51100 41.68%
../test_comp/MAIN.BIN           69737 118784 58.71%
../test_comp/simple.txt            20     96 20.83%
../test_comp/sprite_intro        3339  14984 22.28%
../test_comp/Zombies.SHP        57127 245720 23.25%

New MatchFinder (max match 64)
../test_comp/80croc.def         20722 170782 12.13% -lh5-
../test_comp/BLOX1.DAT          41545 112384 36.97% -lh5-
../test_comp/data.adpcm        518493 570307 90.91% -lh5-
../test_comp/Green Eggs and Ham.txt    716   3475 20.60% -lh5-
../test_comp/jp2_000           126487 267264 47.33% -lh5-
../test_comp/jp2_001            17031  34468 49.41% -lh5-
../test_comp/jp2_002            21288  51100 41.66% -lh5-
../test_comp/MAIN.BIN           69713 118784 58.69% -lh5-
../test_comp/simple.txt            21     96 21.88% -lh5-
../test_comp/sprite_intro        3327  14984 22.20% -lh5-
../test_comp/Zombies.SHP        56020 245720 22.80% -lh5-
../test_comp/Green Eggs and Ham.txt    716   3475 20.60% -lh7-

Dynamic cost model:
../test_comp/80croc.def         20393 170782 11.94% -lh5-
../test_comp/BLOX1.DAT          41027 112384 36.51% -lh5-
../test_comp/data.adpcm        518159 570307 90.86% -lh5-
../test_comp/Green Eggs and Ham.txt    710   3475 20.43% -lh5-
../test_comp/jp2_000           126135 267264 47.19% -lh5-
../test_comp/jp2_001            16978  34468 49.26% -lh5-
../test_comp/jp2_002            21190  51100 41.47% -lh5-
../test_comp/MAIN.BIN           69412 118784 58.44% -lh5-
../test_comp/simple.txt            18     96 18.75% -lh5-
../test_comp/sprite_intro        3310  14984 22.09% -lh5-
../test_comp/Zombies.SHP        55195 245720 22.46% -lh5-
../test_comp/Green Eggs and Ham.txt    710   3475 20.43% -lh7-

Dynamic cost model (best of 20 iterations):
../test_comp/80croc.def         20173 170782 11.81% -lh5-
../test_comp/BLOX1.DAT          40804 112384 36.31% -lh5-
../test_comp/data.adpcm        518160 570307 90.86% -lh5-
../test_comp/Green Eggs and Ham.txt    702   3475 20.20% -lh5-
../test_comp/jp2_000           125988 267264 47.14% -lh5-
../test_comp/jp2_001            16790  34468 48.71% -lh5-
../test_comp/jp2_002            21096  51100 41.28% -lh5-
../test_comp/MAIN.BIN           69308 118784 58.35% -lh5-
../test_comp/simple.txt            21     96 21.88% -lh5-
../test_comp/sprite_intro        3298  14984 22.01% -lh5-
../test_comp/Zombies.SHP        54995 245720 22.38% -lh5-
../test_comp/Green Eggs and Ham.txt    702   3475 20.20% -lh7-

Dynamic lit cost (only):
../test_comp/80croc.def         20491 170782 12.00% -lh5-
../test_comp/BLOX1.DAT          41017 112384 36.50% -lh5-
../test_comp/data.adpcm        518270 570307 90.88% -lh5-
../test_comp/Green Eggs and Ham.txt    715   3475 20.58% -lh5-
../test_comp/jp2_000           126535 267264 47.34% -lh5-
../test_comp/jp2_001            16888  34468 49.00% -lh5-
../test_comp/jp2_002            21160  51100 41.41% -lh5-
../test_comp/MAIN.BIN           69709 118784 58.69% -lh5-
../test_comp/simple.txt            17     96 17.71% -lh5-
../test_comp/sprite_intro        3342  14984 22.30% -lh5-
../test_comp/Zombies.SHP        56041 245720 22.81% -lh5-
../test_comp/Green Eggs and Ham.txt    715   3475 20.58% -lh7-

Tweaked match length cost:
../test_comp/80croc.def         20484 170782 11.99% -lh5-
../test_comp/BLOX1.DAT          41056 112384 36.53% -lh5-
../test_comp/data.adpcm        518487 570307 90.91% -lh5-
../test_comp/Green Eggs and Ham.txt    714   3475 20.55% -lh5-
../test_comp/jp2_000           126224 267264 47.23% -lh5-
../test_comp/jp2_001            16865  34468 48.93% -lh5-
../test_comp/jp2_002            21149  51100 41.39% -lh5-
../test_comp/MAIN.BIN           69583 118784 58.58% -lh5-
../test_comp/simple.txt            18     96 18.75% -lh5-
../test_comp/sprite_intro        3336  14984 22.26% -lh5-
../test_comp/Zombies.SHP        56051 245720 22.81% -lh5-
../test_comp/Green Eggs and Ham.txt    714   3475 20.55% -lh7-
*/

#include "huffcoder.h"
int main()
{
    try {
        const std::string dir = "../test_comp/";
        test_obs();
        test_file("empty", std::vector<uint8_t> {}, LHA_METHOD_LH5);
        test_file("one char", std::vector<uint8_t> {'x'}, LHA_METHOD_LH5);
        //test_recompress();
        test_dir(dir, LHA_METHOD_LH5);
        test_file(dir + "80croc.def", LHA_METHOD_LH7);
        test_file(dir + "80croc.def", LHA_METHOD_LH4);
    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
