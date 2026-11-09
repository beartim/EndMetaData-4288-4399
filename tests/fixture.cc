#include <fstream>
#include <iterator>
#include <memory>
#include <string>

#include "leveldb/db.h"
#include "leveldb/zlib_compressor.h"

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    std::ifstream input(argv[3], std::ios::binary);
    if (!input) return 1;
    const std::string payload{std::istreambuf_iterator<char>(input), {}};

    leveldb::ZlibCompressorRaw compressor;
    leveldb::Options options;
    options.create_if_missing = true;
    options.compressors[0] = &compressor;
    leveldb::DB* opened = nullptr;
    if (!leveldb::DB::Open(options, argv[1], &opened).ok()) return 1;
    const std::unique_ptr<leveldb::DB> db(opened);
    if (!db->Put({}, argv[2], payload).ok()) return 1;
    db->CompactRange(nullptr, nullptr);
    return 0;
}
