#include <print>
#include <filesystem>
#include "fileio.h"
#include "lhafile.h"
#include "decompress.h"

namespace fs = std::filesystem;

enum Command {
    COMMAND_NONE,
    COMMAND_LIST,
    COMMAND_TEST,
    COMMAND_XTRACT,
};

struct Options {
    Command command;
    const char* archive;
    bool quiet;
};

template<typename F>
void foreach_archive_file(const std::string& filename, F callback)
{
    auto data = read_file(filename);
    LhaFileReader lha { data.data(), data.size() };
    for (LhaHeader hdr; lha.next(hdr);)
        callback(data, hdr);
}

static void print_header(const Options& opts)
{
    if (opts.quiet)
        return;
    std::println("Original  Packed Ratio Date      Time     Name");
    std::println("-------- ------- ----- --------- -------- ------------");
}

static void print_file_info(const Options& opts, const LhaHeader& hdr)
{
    if (opts.quiet)
        return;
    double ratio = hdr.original_size ? 1. - (double)hdr.compressed_size / hdr.original_size : 0;
    std::println("{:8d} {:7d} {:4.1f}% {} {} {}{}", hdr.original_size, hdr.compressed_size, ratio * 100, lha_date_str(hdr.mod_date), lha_time_str(hdr.mod_time), hdr.dirname, hdr.filename.c_str());
}

static int list_archive(const Options& opts, int, char**)
{
    print_header(opts);
    foreach_archive_file(opts.archive, [&](const std::vector<uint8_t>&, const LhaHeader& hdr) {
        print_file_info(opts, hdr);
    });
    return 0;
}

static int test_archive(const Options& opts, int, char**)
{
    foreach_archive_file(opts.archive, [&](const std::vector<uint8_t>& lha_file, const LhaHeader& hdr) {
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

static int xtract_archive(const Options& opts, int, char**)
{
    print_header(opts);
    foreach_archive_file(opts.archive, [&](const std::vector<uint8_t>& lha_file, const LhaHeader& hdr) {
        print_file_info(opts, hdr);
        fs::create_directories(hdr.dirname);
        if (lha_method_from_id(hdr.compression_method) == LHA_METHOD_DIR)
            return;
        const auto data = decompress(lha_file, hdr);
        write_file(hdr.dirname + hdr.filename, data);
    });
    return 0;
}

static Command parse_command(const char* arg)
{
    if (arg[1] == 0) {
        switch (arg[0]) {
        case 'l':
            return COMMAND_LIST;
        case 't':
            return COMMAND_TEST;
        case 'x':
            return COMMAND_XTRACT;
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
            if (!opts.command)
                opts.command = parse_command(arg);
            else if (!opts.archive)
                opts.archive = arg;
            else
                break;
            continue;
        }

        switch (arg[1]) {
        case 'q':
            opts.quiet = true;
            break;
        default:
            std::println("Unknown option {:?}", arg);
            return false;
        }
    }
    return true;
}

int main(int argc, char* argv[])
{
    try {
        Options opts {};
        const char* prog_name = argv[0];
        if (!parse_options(opts, argc, argv) || !opts.command || !opts.archive) {
            std::println("Usage: {} [-<options>] <command> <archive> ...", prog_name);
            return 1;
        }

        using dispatch_function = int (*)(const Options& opts, int argc, char** argv);
        const dispatch_function funcs[] = {
            nullptr,
            &list_archive,
            &test_archive,
            &xtract_archive,
        };
        return funcs[opts.command](opts, argc, argv);
    } catch (const std::exception& e) {
        std::println("{}", e.what());
        return 1;
    }
}
