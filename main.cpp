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

static constexpr uint16_t window_bits = 13; // LH5

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
    if (!std::memcmp(data, out.data(), size))
        return;

    std::println("Decompressed failed!");
    exit(1);
}

#include "decompress.h"
void test_file(const std::string& filename)
{
    auto data = read_file(filename);
    const auto lz = lz_build(data.data(), (uint32_t)data.size(), window_bits);
    test_lz(data.data(), (uint32_t)data.size(), lz);
    const auto encoded = encode_lh(lz, window_bits);
    const auto decoded = decompress(encoded.data(), (uint32_t)encoded.size(), (uint32_t)data.size(), LHA_METHOD_LH5);

    if (decoded != data)
        throw std::runtime_error { std::format("Wrong data decoded for {}", filename) };

    std::println("{:30s} {:6d} {:6d} {:.2f}%", filename, encoded.size(), decoded.size(), 100.*encoded.size() / decoded.size());

}

#include <filesystem>
void test_dir(const std::string& dir_path)
{
    namespace fs = std::filesystem;
    for (const auto& e : fs::recursive_directory_iterator { dir_path })
        test_file(e.path().string());
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
*/

int main()
{
    try {
        test_obs();
        const std::string dir = "../test_comp/";
        //test_file(dir + "Green Eggs and Ham.txt");
        test_dir(dir);
    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
