#include <print>
#include <filesystem>
#include <map>
#include <chrono>
#include "fileio.h"
#include "lhafile.h"
#include "decompress.h"
#include "lhacompress.h"

// https://web.archive.org/web/20110817112842/http://homepage1.nifty.com/dangan/en/Content/Program/Java/jLHA/Notes/Notes.html

namespace fs = std::filesystem;

using dispatch_function = int (*)(const struct Options& opts, int argc, char** argv);

struct Options {
    dispatch_function action;
    const char* archive;
    bool quiet;
    bool recursive;
    LhaMethod compression_method = LHA_METHOD_LH5;
};

template<typename F>
void foreach_archive_file(const std::string& filename, F callback)
{
    auto data = read_file(filename);
    LhaFileReader lha { data.data(), data.size() };
    for (LhaHeader hdr; lha.next(hdr);)
        callback(data, hdr);
}

struct RunningTotal {
    uint32_t total_orig = 0;
    uint32_t total_packed = 0;
    uint32_t num_files = 0;

    void update(const LhaHeader& hdr)
    {
        ++num_files;
        total_orig += hdr.original_size;
        total_packed += hdr.compressed_size;
    }
};

static RunningTotal running_total;

static double calc_ratio(uint32_t orig, uint32_t packed)
{
    if (!orig)
        return 0;
    return 100. * packed / orig; // Amiga LHA shows 1-packed/orig
}

static void print_header(const Options& opts)
{
    if (opts.quiet)
        return;
    std::println("Original  Packed Ratio Method Date      Time     Name");
    std::println("-------- ------- ----- ------ --------- -------- ------------");
}

static void print_footer(const Options& opts)
{
    if (opts.quiet)
        return;
    std::println("-------------------------------------------------------------");
    std::println("{:8d} {:7d} {:4.1f}% {:25s} {}, {} files",
        running_total.total_orig,
        running_total.total_packed,
        calc_ratio(running_total.total_orig, running_total.total_packed),
        "",
        opts.archive,
        running_total.num_files);
}

static void print_file_info(const Options& opts, const LhaHeader& hdr)
{
    if (opts.quiet)
        return;
    std::println("{:8d} {:7d} {:4.1f}% {:6s} {} {} {}{}",
        hdr.original_size,
        hdr.compressed_size,
        calc_ratio(hdr.original_size, hdr.compressed_size),
        std::string(hdr.compression_method, hdr.compression_method+5),
        lha_date_str(hdr.mod_date),
        lha_time_str(hdr.mod_time),
        hdr.dirname, hdr.filename.c_str());

    running_total.update(hdr);
}

struct GlobResult {
    fs::directory_entry dir_entry;
    PathComponents paths;
};

template<typename Iter>
static void glob_impl(std::vector<GlobResult>& files, const char* path_pattern)
{
    auto [dirname, pattern] = split_path(path_pattern);
    if (dirname.empty())
        dirname = ".";

    const auto base = fs::path { dirname };
    for (const auto& dir_entry : Iter { base }) {
        if (!dir_entry.is_regular_file())
            continue;
        const auto fn = dir_entry.path().filename().string();
        if (wildcard_match(pattern.c_str(), fn.c_str())) {
            GlobResult gr;
            gr.dir_entry = dir_entry;
            gr.paths = split_path(fs::relative(dir_entry.path(), base).string());
            files.emplace_back(std::move(gr));
        }
    }
}

static void glob(std::vector<GlobResult>& files, const char* path_pattern, bool recursive)
{
    if (recursive)
        return glob_impl<fs::recursive_directory_iterator>(files, path_pattern);    
    else 
        return glob_impl<fs::directory_iterator>(files, path_pattern);
}

static std::vector<GlobResult> glob_all(const Options& opts, int argc, char** argv)
{
    std::vector<GlobResult> files;
    while (argc--)
        glob(files, *argv++, opts.recursive);
    return files;
}

static bool check_included(const Options&, const LhaHeader& hdr, int argc, char** argv)
{
    if (!argc)
        return true;
    const auto fn = hdr.dirname + hdr.filename;
    while (argc--)
        if (wildcard_match(*argv++, fn.c_str()))
            return true;
    return false;
}

struct ArchiveInfo {
    std::vector<uint8_t> data;
    std::map<std::string, LhaHeader, FilenameCompare> files;
};

static ArchiveInfo read_existing_archive(const std::string& filename)
{
    ArchiveInfo arc {};
    try {
        arc.data = read_file(filename);
    } catch (const FileOpenException& e) {
        if (e.err_not_found())
            return arc;
        throw;
    }

    LhaFileReader lha { arc.data.data(), arc.data.size() };
    for (LhaHeader hdr; lha.next(hdr);) {
        std::string path = hdr.dirname + hdr.filename.c_str(); // N.B. there can be comments after an embedded NUL byte
        if (!arc.files.insert({ path, std::move(hdr) }).second)
            throw std::runtime_error { std::format("The archive {:?} contains duplicate file entry {:?}", filename, path) };
    }

    return arc;
}

static int add_or_update(const Options& opts, int argc, char** argv, bool update)
{
    const auto files = glob_all(opts, argc, argv);
    if (files.empty()) {
        std::println("No files found matching pattern(s)");
        return 1;
    }

    ArchiveInfo arc = read_existing_archive(opts.archive);
    std::vector<const LhaHeader*> keep;
    if (update) {
        for (const auto& [_, hdr] : arc.files)
            keep.push_back(&hdr);
    }

    for (const auto& [_, p] : files) {
        const auto fullname = p.dirname + p.filename;
        auto it = arc.files.find(fullname);
        const bool found = it != arc.files.end();
        if (found != update)
            throw std::runtime_error { std::format("{} {} in archive {:?}", fullname, update ? "doesn't exist" : "already exists", opts.archive) };
        if (update) {
            std::erase(keep, &it->second);
            arc.files.erase(it);
        }
    }

    print_header(opts);

    if (update) {
        std::vector<uint8_t> new_data;
        for (const auto k : keep) {
            auto file_data = &arc.data[k->header_offset];
            new_data.insert(new_data.end(), file_data, file_data + k->compressed_offset + k->compressed_size - k->header_offset);
            //print_file_info(opts, *k);
        }
        arc.data.swap(new_data);
    }

    for (const auto& [de, p] : files) {
        auto modtime = std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(de.last_write_time()));
        const auto start_pos = arc.data.size();
        lha_compress(arc.data, read_file(de.path().string()), p.dirname, p.filename, opts.compression_method, modtime);

        LhaFileReader fr { &arc.data[start_pos], arc.data.size() - start_pos };
        LhaHeader hdr;
        if (!fr.next(hdr))
            throw std::runtime_error { "Internal error: could not read back header" };

        print_file_info(opts, hdr);
    }
    print_footer(opts);

    write_file(opts.archive, arc.data);

    return 0;
}

static int add_archive(const Options& opts, int argc, char** argv)
{
    return add_or_update(opts, argc, argv, false);
}

static int update_archive(const Options& opts, int argc, char** argv)
{
    return add_or_update(opts, argc, argv, true);
}

static int list_archive(const Options& opts, int argc, char** argv)
{
    print_header(opts);
    foreach_archive_file(opts.archive, [&](const std::vector<uint8_t>&, const LhaHeader& hdr) {
        if (!check_included(opts, hdr, argc, argv))
            return;
        print_file_info(opts, hdr);
    });
    print_footer(opts);
    return 0;
}

static int test_archive(const Options& opts, int argc, char** argv)
{
    foreach_archive_file(opts.archive, [&](const std::vector<uint8_t>& lha_file, const LhaHeader& hdr) {
        if (!check_included(opts, hdr, argc, argv))
            return;
        if (!opts.quiet) {
            std::print("{}{} ... ", hdr.dirname, hdr.filename.c_str());
            fflush(stdout);
        }
        decompress(lha_file, hdr);
        if (!opts.quiet)
            std::println("OK!");
    });
    return 0;
}

static int extract(const Options& opts, int argc, char** argv, bool with_path)
{
    print_header(opts);
    foreach_archive_file(opts.archive, [&](const std::vector<uint8_t>& lha_file, const LhaHeader& hdr) {
        if (!check_included(opts, hdr, argc, argv))
            return;
        print_file_info(opts, hdr);
        if (with_path && !hdr.dirname.empty())
            fs::create_directories(hdr.dirname);
        if (lha_method_from_id(hdr.compression_method) == LHA_METHOD_DIR)
            return;
        const auto data = decompress(lha_file, hdr);
        write_file((with_path ? hdr.dirname : "") + hdr.filename, data);
    });
    print_footer(opts);
    return 0;
}

static int extract_with_path(const Options& opts, int argc, char** argv)
{
    return extract(opts, argc, argv, true);
}

static int extract_without_path(const Options& opts, int argc, char** argv)
{
    return extract(opts, argc, argv, false);
}

static dispatch_function parse_command(const char* arg)
{
    if (arg[1] == 0) {
        switch (arg[0]) {
        case 'a':
            return &add_archive;
        case 'e':
            return &extract_without_path;
        case 'l':
            return &list_archive;
        case 't':
            return &test_archive;
        case 'u':
            return &update_archive;
        case 'x':
            return &extract_with_path;
        }
    }
    throw std::runtime_error { std::format("Unknown command {:?}", arg) };
}

static bool parse_options(Options& opts, int& argc, char**& argv)
{
    --argc;
    ++argv;

    for (; argc; --argc, ++argv) {
        const char* arg = argv[0];
        if (arg[0] != '-') {
            if (!opts.action)
                opts.action = parse_command(arg);
            else if (!opts.archive)
                opts.archive = arg;
            else
                break;
            continue;
        }

        for (const char* a = &arg[1]; *a; ++a) {
            switch (*a) {
            case 'q':
                opts.quiet = true;
                break;
            case 'r':
                opts.recursive = true;
                break;
            case '4':
                opts.compression_method = LHA_METHOD_LH4;
                break;
            case '5':
                opts.compression_method = LHA_METHOD_LH5;
                break;
            case '6':
                opts.compression_method = LHA_METHOD_LH6;
                break;
            case '7':
                opts.compression_method = LHA_METHOD_LH7;
                break;
            default:
                std::println("Unknown option {} in {:?}", *a, arg);
                return false;
            }
        }
    }
    return true;
}

int main(int argc, char* argv[])
{
    try {
        Options opts {};
        const char* prog_name = argv[0];
        if (!parse_options(opts, argc, argv) || !opts.action || !opts.archive) {
            std::println("Usage: {} [-<options>] <command> <archive> [files...]", prog_name);
            return 1;
        }
        opts.action(opts, argc, argv);
    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
