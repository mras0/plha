#include <print>
#include <string>
#include <stdexcept>
#include <vector>
#include <cassert>
#include "fileio.h"
#include "lhafile.h"
#include "decompress.h"

#include "ibs.h"

template<uint16_t NumSyms>
class DynHuffTree {
public:
    static constexpr uint16_t num_symbols = NumSyms;

    explicit DynHuffTree()
    {
        // Leaves
        for (uint16_t sym = 0; sym < num_symbols; ++sym) {
            freq_[sym] = 1;
            child_[sym] = sym + tree_size;
            parent_[sym + tree_size] = sym;
        }
        // Internal nodes
        for (uint16_t c = 0, p = num_symbols; p < tree_size; ++p, c += 2) {
            freq_[p] = freq_[c] + freq_[c + 1];
            child_[p] = c;
            parent_[c] = parent_[c + 1] = p;
        }

        freq_[tree_size] = 0xffff; // sentinel
        parent_[root] = 0;
    }

    void update(uint16_t sym)
    {
        if (freq_[root] == max_freq)
            reconstruct();
        uint16_t node = parent_[sym + tree_size];
        do {
            uint16_t node_freq = ++freq_[node];
            uint16_t next = node + 1; // sibling
            if (node_freq > freq_[next]) {
                while (node_freq > freq_[next + 1])
                    ++next;
                freq_[node] = freq_[next];
                freq_[next] = node_freq;

                uint16_t node_child = child_[node];
                parent_[node_child] = next;
                if (node_child < tree_size)
                    parent_[node_child + 1] = next;

                uint16_t next_child = child_[next];
                child_[next] = node_child;

                parent_[next_child] = node;
                if (next_child < tree_size)
                    parent_[next_child + 1] = node;
                child_[node] = next_child;

                node = next;
            }
            node = parent_[node];
        } while (node);
    }

    void print_info()
    {
        std::println("i   ch   fr");
        for (int i = 0; i < tree_size; ++i)
            std::println("{:2d} {:2d} {:2d}", i, child_[i], freq_[i]);
        std::println("i   parent");
        for (int i = 0; i < tree_size + num_symbols; ++i)
            std::println("{:2d} {:2d}", i, parent_[i]);
        std::println("tree_size {} root {}", tree_size, root);
    }

    void print()
    {
        do_print("", root, false);
    }

    void do_print(const std::string& prefix, int node, bool is_left, int level = 0)
    {
        const int ch = child_[node];
        std::print("{}{} ", prefix, is_left ? "|--" : "+--");
        if (ch >= tree_size) {
            std::println("sym {}, freq {}", ch - tree_size, freq_[node]);
            return;
        }
        std::println("node {}, freq {}", node, freq_[node]);
        const auto new_prefix = prefix + (is_left ? "|  " : "   ");
        do_print(new_prefix, ch, true, level + 1);
        do_print(new_prefix, ch + 1, false, level + 1);
    }

    std::string code_str(uint16_t sym)
    {
        assert(sym < NumSyms);
        uint16_t node = parent_[sym + tree_size];
        std::string res;
        while (node != root) {
            res.insert(res.begin(), (char)('0' + (node & 1)));
            node = parent_[node];
        }
        return res;
    }

    uint16_t decode(InputBitString& ibs)
    {
        uint16_t node = child_[root];
        while (node < tree_size)
            node = child_[node + ibs.get(1)];
        node -= tree_size;
        update(node);
        return node;
    }

    void reconstruct()
    {
        // Collect leaves in first half and halve frequency
        for (uint16_t node = 0, nsyms = 0; node < tree_size; ++node) {
            if (child_[node] < tree_size)
                continue;
            freq_[nsyms] = (freq_[node] + 1) >> 1;
            child_[nsyms] = child_[node];
            ++nsyms;
        }
        // Reconstruct tree
        for (uint16_t c = 0, p = num_symbols; p < tree_size; ++p, c += 2) {
            uint16_t f = freq_[p] = freq_[c] + freq_[c + 1];
            uint16_t k;
            for (k = p - 1; f < freq_[k]; --k)
                ;
            k++;
            uint16_t l = (p - k) * sizeof(uint16_t);
            memmove(&freq_[k + 1], &freq_[k], l);
            freq_[k] = f;
            memmove(&child_[k + 1], &child_[k], l);
            child_[k] = c;
        }
        // Reconnect parent
        for (uint16_t i = 0; i < tree_size; ++i) {
            uint16_t c = child_[i];
            if (c >= tree_size) {
                parent_[c] = i;
            } else {
                parent_[c] = parent_[c + 1] = i;
            }
        }
    }

    static constexpr uint16_t tree_size = num_symbols * 2 - 1;
    static constexpr uint16_t root = tree_size - 1;
    static constexpr uint16_t max_freq = 0x8000;

    uint16_t freq_[tree_size + 1]; // freq[i] <= freq[i+1]
    uint16_t parent_[tree_size + num_symbols]; // tree_size..tree_size+num_symbols-1 contain the starting node for a symbol
    uint16_t child_[tree_size];
};

// N        4096
// F        60
// TRESHOLD 2
// NIL      N
// N_CHAR   (256 - TRESHOLD + F) = 314
// T        (N_CHAR * 2 - 1)  = 627
// R        (T - 1) = 626
// MAX_FREQ 0x8000

const uint8_t d_code[256] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
	0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09, 0x09,
	0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
	0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B, 0x0B,
	0x0C, 0x0C, 0x0C, 0x0C, 0x0D, 0x0D, 0x0D, 0x0D,
	0x0E, 0x0E, 0x0E, 0x0E, 0x0F, 0x0F, 0x0F, 0x0F,
	0x10, 0x10, 0x10, 0x10, 0x11, 0x11, 0x11, 0x11,
	0x12, 0x12, 0x12, 0x12, 0x13, 0x13, 0x13, 0x13,
	0x14, 0x14, 0x14, 0x14, 0x15, 0x15, 0x15, 0x15,
	0x16, 0x16, 0x16, 0x16, 0x17, 0x17, 0x17, 0x17,
	0x18, 0x18, 0x19, 0x19, 0x1A, 0x1A, 0x1B, 0x1B,
	0x1C, 0x1C, 0x1D, 0x1D, 0x1E, 0x1E, 0x1F, 0x1F,
	0x20, 0x20, 0x21, 0x21, 0x22, 0x22, 0x23, 0x23,
	0x24, 0x24, 0x25, 0x25, 0x26, 0x26, 0x27, 0x27,
	0x28, 0x28, 0x29, 0x29, 0x2A, 0x2A, 0x2B, 0x2B,
	0x2C, 0x2C, 0x2D, 0x2D, 0x2E, 0x2E, 0x2F, 0x2F,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
};

const uint8_t d_len[256] = {
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06, 0x06,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07, 0x07,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
	0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,
};


#include "util.h"
#include "crc16.h"
std::vector<uint8_t> do_lh1(const uint8_t* data, uint32_t compressed_size, uint32_t uncompressed_size)
{
    InputBitString ibs { data, compressed_size };
    (void)uncompressed_size;
    DynHuffTree<256 + 60 - 2> tr {};
    static_assert(tr.num_symbols == 314);

    std::vector<uint8_t> result(uncompressed_size);

    for (uint32_t pos = 0; pos < uncompressed_size;) {
        uint16_t sym;
        try {
            sym = tr.decode(ibs);
        } catch (...) {
            hexdump(result.data(), pos);
            break;
        }
        if (sym < 256) {
            result[pos++] = (uint8_t)sym;
            continue;
        }

        uint16_t i = ibs.get(8);
        uint16_t c = d_code[i] << 6;
        uint16_t j = d_len[i] - 2;
        while (j--)
            i = (i << 1) | ibs.get(1);
        uint16_t ofs = (c | (i & 0x3f)) + 1;
        uint16_t len = min_match_len + sym - 256;

        //std::println("{} {}", ofs, sym - 256 + min_match_len);
        //if (ofs < pos)
        //    throw std::runtime_error{std::format("Invalid ofs {}
        if (pos + len > uncompressed_size)
            throw std::runtime_error { "Match is too long!" };
        while (ofs > pos && len) {
            // The dictionary is initially filled with spaces...
            result[pos++] = ' ';
            len--;
        }
           
        for (; len--; ++pos)
            result[pos] = result[pos - ofs];
    }

    return result;
}

void test_lh1_file(const std::string& filename)
{
    #if 0
    (void)filename;
    DynHuffTree<4> tr {};
    tr.print_info();
    tr.print();
    for (int i = 0; i < 3; ++i) {
        std::println("-------------------");
        tr.update(1);
        tr.print();
    }
    for (int i = 0; i < 4; ++i) {
        std::println("-------------------");
        tr.update(0);
        tr.print();
    }
    tr.print_info();

    for (uint16_t i = 0; i < tr.num_symbols; ++i)
        std::println("{:2d} {}", i, tr.code_str(i));
    #endif

    auto data = read_file(filename);
    LhaFileReader lha { data.data(), data.size() };
    for (LhaHeader hdr; lha.next(hdr);) {
        std::println("{:5.5s} {:6d} {:6d} {}{}", (const char*)hdr.compression_method, hdr.compressed_size, hdr.original_size, hdr.dirname, hdr.filename);
        if (lha_method_from_id(hdr.compression_method) != LHA_METHOD_LH1)
            continue;
        auto res = do_lh1(data.data() + hdr.compressed_offset, hdr.compressed_size, hdr.original_size);
        if (const auto crc = crc16(res.data(), res.size()); crc != hdr.crc)
            throw std::runtime_error {std::format("CRC mismatch ${:04X} != ${:04X}", crc, hdr.crc )};
    }
}

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
void test_dir(const std::string& dir_path)
{
    namespace fs = std::filesystem;
    for (const auto& e : fs::recursive_directory_iterator { dir_path, fs::directory_options::skip_permission_denied }) {
        try {
            if (e.is_directory())
                continue;
            const auto path = e.path();
            if (path.extension() != ".lha")
                continue;
            if (path.filename() == "im-tools.lha") // No CRC?
                continue;
            if (path.filename().u8string().starts_with(u8"bgui")) // AROS fake .lha's
                continue;
        } catch (const std::exception& ex) {
            std::println("{} - {}", e.path().string(), ex.what());
            continue;
        }

        test_file(e.path().string());
    }
}

int main()
{
    try {
        //
        test_lh1_file("../test_decomp/PPDecrunch10.lzh");
        test_lh1_file("../test_decomp/imploder-4.0.lzh");
        test_lh1_file("../test_decomp/glowria.lzh");
        test_dir("../test_decomp");
    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
