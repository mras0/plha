#include "lhacompress.h"
#include "lhaheader.h"
#include "lhafile.h"
#include "compress.h"
#include "crc16.h"
#include <cstring>

void lha_compress(std::vector<uint8_t>& output, const std::vector<uint8_t>& uncompressed_data, const std::string& dirname, const std::string& filename, int64_t modtime, const LhaCompressOptions& options)
{
    auto method = options.method;
    auto compressed_data = compress(uncompressed_data.data(), (uint32_t)uncompressed_data.size(), method);
    if (options.method != LHA_METHOD_LH0 && (uncompressed_data.empty() || compressed_data.size() * 100 / uncompressed_data.size() >= options.max_ratio_percent)) {
        method = LHA_METHOD_LH0;
        compressed_data = uncompressed_data;
    }
    LhaHeader hdr {};
    hdr.filename = filename;
    hdr.dirname = dirname;
    hdr.level = 1;
    hdr.os = lha_os_amiga;
    hdr.compressed_size = (uint32_t)compressed_data.size();
    hdr.original_size = (uint32_t)uncompressed_data.size();
    hdr.crc = crc16(uncompressed_data.data(), uncompressed_data.size());
    lha_header_set_dos_time(hdr, modtime);
    std::memcpy(hdr.compression_method, lha_method_names[method], sizeof(hdr.compression_method));
    lha_header_append(output, hdr);
    output.insert(output.end(), compressed_data.begin(), compressed_data.end());
}
