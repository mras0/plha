#include <print>
#include <filesystem>
#include <map>
#include <algorithm>
#include <cstring>
#include <cassert>
#include "fileio.h"
#include "lhafile.h"
#include "decompress.h"
#include "lhacompress.h"
#include "attrs.h"

// https://web.archive.org/web/20110817112842/http://homepage1.nifty.com/dangan/en/Content/Program/Java/jLHA/Notes/Notes.html

namespace fs = std::filesystem;

using dispatch_function = int (*)(const struct Options& opts, int argc, char** argv);

struct Options {
    dispatch_function action;
    const char* archive;
    bool quiet;
    bool recursive;
    LhaCompressOptions compression_options;
};

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
    std::println("Original  Packed  Ratio Method Date      Time     Name");
    std::println("-------- ------- ------ ------ --------- -------- ------------");
}

static void print_footer(const Options& opts)
{
    if (opts.quiet)
        return;
    std::println("-------------------------------------------------------------");
    std::println("{:8d} {:7d} {:5.1f}% {:25s} {}, {} files",
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
    std::println("{:8d} {:7d} {:5.1f}% {:6s} {} {} {}{}",
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

enum class UpdateMode {
    add,
    replace,
    update,
};

static int add_or_update(const Options& opts, int argc, char** argv, UpdateMode mode)
{
    const auto glob_files = glob_all(opts, argc, argv);
    if (glob_files.empty()) {
        std::println("No files found matching pattern(s)");
        return 1;
    }

    ArchiveInfo arc = read_existing_archive(opts.archive);
    std::vector<const LhaHeader*> keep;
    if (mode != UpdateMode::add) { // When adding all files are kept
        for (const auto& [_, hdr] : arc.files)
            keep.push_back(&hdr);
    }
    struct FileToAdd {
        fs::path path;
        std::string dirname;
        FileAttributes attrs;
    };
    std::vector<FileToAdd> files;

    for (const auto& [de, p] : glob_files) {
        FileToAdd f {};
        f.path = de.path();
        f.dirname = p.dirname;
        f.attrs = attrs_get(de.path().c_str());
        if (f.attrs.name.empty())
            f.attrs.name = p.filename;

        const auto fullname = p.dirname + f.attrs.name;
        auto it = arc.files.find(fullname);
        const bool found = it != arc.files.end();
        switch (mode) {
        case UpdateMode::add:
            if (found)
                throw std::runtime_error { std::format("{} already exists in archive {:?}", fullname, opts.archive) };
            break;
        case UpdateMode::update:
            if (!found)
                throw std::runtime_error { std::format("{} doesn't exist in archive {:?}", fullname, opts.archive) };
            break;
        case UpdateMode::replace:
            break;
        }
        if (found) {
            std::erase(keep, &it->second);
            arc.files.erase(it);
        }

        files.emplace_back(std::move(f));
    }

    print_header(opts);

    if (mode != UpdateMode::add) {
        std::vector<uint8_t> new_data;
        for (const auto k : keep) {
            auto file_data = &arc.data[k->header_offset];
            new_data.insert(new_data.end(), file_data, file_data + k->compressed_offset + k->compressed_size - k->header_offset);
        }
        arc.data.swap(new_data);
    }
    for (const auto& f : files) {
        const auto start_pos = arc.data.size();

        auto name = f.attrs.name;
        if (!f.attrs.comment.empty()) {
            name += '\0';
            name += f.attrs.comment;
        }
        lha_compress(arc.data, read_file(f.path.string()), f.dirname, name, f.attrs.modtime, f.attrs.protect, opts.compression_options);

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
    return add_or_update(opts, argc, argv, UpdateMode::add);
}

static int update_archive(const Options& opts, int argc, char** argv)
{
    return add_or_update(opts, argc, argv, UpdateMode::update);
}

static int replace_archive(const Options& opts, int argc, char** argv)
{
    return add_or_update(opts, argc, argv, UpdateMode::replace);
}

template <typename F>
void foreach_archive_file(const Options& opts, int argc, char** argv, F callback)
{
    auto lha_file = read_file(opts.archive);
    LhaFileReader lha { lha_file.data(), lha_file.size() };
    for (LhaHeader hdr; lha.next(hdr);) {
        if (check_included(opts, hdr, argc, argv))
            callback(lha_file, hdr);
    }
}

static int list_archive(const Options& opts, int argc, char** argv)
{
    print_header(opts);
    foreach_archive_file(opts, argc, argv, [&](const std::vector<uint8_t>&, const LhaHeader& hdr) {
        print_file_info(opts, hdr);
    });
    print_footer(opts);
    return 0;
}

static std::string os_string(uint8_t os)
{
    switch (os) {
    case 0:
        return "Unknown";
    case lha_os_amiga: // 'A'
        return "Amiga";
    case 'M':
        return "MS-DOS";
    case 'U':
        return "Unix";
    default:
        return std::format("0x{:02X}", os);
    }
}

static int list_verbose(const Options& opts, int argc, char** argv)
{
    std::println("Original  Packed  Ratio Date      Time       Atts   Method CRC  Host OS  L");
    std::println("-------- ------- ------ --------- -------- -------- ------ ---- -------- ---");

    foreach_archive_file(opts, argc, argv, [&](const std::vector<uint8_t>&, const LhaHeader& hdr) {
        std::println("{:8d} {:7d} {:5.1f}% {} {} {} {:6s} {:04X} {:8} {}\n{}{}",
            hdr.original_size,
            hdr.compressed_size,
            calc_ratio(hdr.original_size, hdr.compressed_size),
            lha_date_str(hdr.mod_date),
            lha_time_str(hdr.mod_time),
            protect_string(hdr.protect),
            std::string(hdr.compression_method, hdr.compression_method + 5),
            hdr.crc,
            os_string(hdr.os),
            hdr.level,
            hdr.dirname, hdr.filename.c_str());

        running_total.update(hdr);
    });
    std::println("-------- ------- ------ --------- -------- {:5} file{}", running_total.num_files, running_total.num_files == 1 ? "" : "s");
    std::println("{:8d} {:7d} {:5.1f}%",
        running_total.total_orig,
        running_total.total_packed,
        calc_ratio(running_total.total_orig, running_total.total_packed));
    
    return 0;
}


static int test_archive(const Options& opts, int argc, char** argv)
{
    foreach_archive_file(opts, argc, argv, [&](const std::vector<uint8_t>& lha_file, const LhaHeader& hdr) {
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
    foreach_archive_file(opts, argc, argv, [&](const std::vector<uint8_t>& lha_file, const LhaHeader& hdr) {
        print_file_info(opts, hdr);
        if (with_path && !hdr.dirname.empty())
            fs::create_directories(hdr.dirname);
        if (lha_method_from_id(hdr.compression_method) == LHA_METHOD_DIR)
            return;
        const auto data = decompress(lha_file, hdr);
        const auto dirname = with_path ? hdr.dirname : "";
        FileAttributes attr {};
        if (auto pos = hdr.filename.find_first_of('\0'); pos < hdr.filename.size() - 1) {
            attr.name = hdr.filename.substr(0, pos);
            attr.comment = hdr.filename.substr(pos + 1);
        } else {
            attr.name = hdr.filename;
        }
        attr.protect = hdr.protect;
        attr.modtime = unix_time_from_dos_time(hdr.mod_date, hdr.mod_time);
        write_with_attributes(data, dirname, attr);
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

static const struct {
    char command_char;
    dispatch_function dispatch;
    const char* description;
} command_table[] = {
    { 'a', &add_archive, "Add to archive" },
    { 'e', &extract_without_path, "Extract (without path)" },
    { 'l', &list_archive, "List archive" },
    { 'r', &replace_archive, "Replace in archive" },
    { 't', &test_archive, "Test archive" },
    { 'u', &update_archive, "Update archive" },
    { 'v', &list_verbose, "Verbose listing" },
    { 'x', &extract_with_path, "Extract (with path)" },
};

struct OptionHandler {
    using PlainType = void (*)(Options& opts);
    using NumType = void (*)(Options& opts, uint32_t number);

    constexpr OptionHandler(PlainType handler)
        : type { TYPE_PLAIN }
        , plain { handler }
    {
    }
    constexpr OptionHandler(NumType handler)
        : type { TYPE_NUM }
        , num { handler }
    {
    }

    enum {
        TYPE_PLAIN,
        TYPE_NUM,
    } type;
    union {
        PlainType plain;
        NumType num;
    };
};

static const struct {
    const char* opt;
    OptionHandler handler;
    const char* description;
} option_table[] = {
    { "q", +[](Options& opts) { opts.quiet = true; }, "Quiet" },
    { "r", +[](Options& opts) { opts.recursive = true; }, "Recursive" },
    { "0", +[](Options& opts) { opts.compression_options.method = LHA_METHOD_LH0; }, "Use -lh0- (no compression)" },
    { "1", +[](Options& opts) { opts.compression_options.method = LHA_METHOD_LH1; }, "Use -lh1-" },
    { "4", +[](Options& opts) { opts.compression_options.method = LHA_METHOD_LH4; }, "Use -lh4-" },
    { "5", +[](Options& opts) { opts.compression_options.method = LHA_METHOD_LH5; }, "Use -lh5-" },
    { "6", +[](Options& opts) { opts.compression_options.method = LHA_METHOD_LH6; }, "Use -lh6-" },
    { "7", +[](Options& opts) { opts.compression_options.method = LHA_METHOD_LH7; }, "Use -lh7-" },
    { "Qr", +[](Options& opts, uint32_t num) { opts.compression_options.max_ratio_percent = num; }, "Max. compressed ratio (%)" },
};

static dispatch_function parse_command(const char* arg)
{
    if (arg[1] == 0) {
        for (const auto& cmd : command_table) {
            if (arg[0] == cmd.command_char)
                return cmd.dispatch;
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

        for (const char* a = &arg[1]; *a;) {
            auto opt = std::find_if(std::begin(option_table), std::end(option_table), [a](const auto& o) {
                return std::strncmp(a, o.opt, std::strlen(o.opt)) == 0;
            });
            if (opt == std::end(option_table)) {
                std::println("Unknown option {} in {:?}", *a, arg);
                return false;
            }
            a += strlen(opt->opt);
            if (opt->handler.type == OptionHandler::TYPE_PLAIN) {
                opt->handler.plain(opts);
                continue;
            }
            assert(opt->handler.type == OptionHandler::TYPE_NUM);
            char* end = nullptr;
            const auto num = (uint32_t)strtoul(a, &end, 10);
            if (!end || a == end) {
                std::println("Expected number got \"{}\" in {:?} for {}", a, arg, opt->description);
                return false;
            }
            a = end;
            opt->handler.num(opts, num);            
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
            static constexpr size_t arg_width = 3;
            static constexpr size_t desc_width = 30;

            std::println("Usage: {} [-<option(s)>] <command> <archive> [files...]", prog_name);
            std::println("");
            std::println("Commands:");
            for (size_t i = 0; i < std::size(command_table); i += 2) {
                auto pr_cmd = [](size_t index) {
                    std::print("  {:{}} {:{}}", command_table[index].command_char, arg_width, command_table[index].description, desc_width);
                };
                pr_cmd(i);
                if (i + 1 != std::size(command_table))
                    pr_cmd(i + 1);
                std::println("");
            }
            std::println("");
            std::println("Options:");
            for (size_t i = 0; i < std::size(option_table); i += 2) {
                auto pr_cmd = [](size_t index) {
                    static_assert(arg_width > 1);
                    std::print("  -{:{}} {:{}}", option_table[index].opt, arg_width - 1, option_table[index].description, desc_width);
                };
                pr_cmd(i);
                if (i + 1 != std::size(option_table))
                    pr_cmd(i + 1);
                std::println("");
            }

            return 1;
        }
        opts.action(opts, argc, argv);
    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
