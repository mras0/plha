#include "lhacompress.h"
#include "lhaheader.h"
#include "lhafile.h"
#include "compress.h"
#include "crc16.h"
#include <cstring>

void lha_compress(std::vector<uint8_t>& output, const std::vector<uint8_t>& uncompressed_data, const std::string& dirname, const std::string& filename, LhaMethod method, int64_t modtime)
{
    auto compressed_data = compress(uncompressed_data.data(), (uint32_t)uncompressed_data.size(), method);
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
