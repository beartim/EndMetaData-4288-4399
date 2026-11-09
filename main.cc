#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef __APPLE__
#include <copyfile.h>
#include <unistd.h>
#endif

#include "db/filename.h"
#include "leveldb/db.h"
#include "leveldb/zlib_compressor.h"

namespace {
namespace fs = std::filesystem;

constexpr std::array<unsigned, 2> targets{4288, 4399};
using Matches = std::array<bool, targets.size()>;

enum class Tag : unsigned {
    End, Byte, Short, Int, Long, Float, Double, ByteArray, String, List, Compound, IntArray, LongArray
};

enum class Scope { Ignore, Root, Data, Limbo, Chunk, Entities, Entity };

class EndMetadata {
public:
    explicit EndMetadata(std::string_view bytes) : remaining_(bytes) {}

    Matches read() {
        if (type() != Tag::Compound) invalid();
        string();  // Root name.
        payload(Tag::Compound, Scope::Root, 0);
        if (!remaining_.empty()) invalid();
        return matches_;
    }

private:
    static constexpr std::array<unsigned, 7> scalar_widths{0, 1, 2, 4, 8, 4, 8};
    std::string_view remaining_;
    Matches matches_{};

    [[noreturn]] static void invalid() { throw std::runtime_error("Invalid TheEnd NBT"); }

    std::string_view take(std::size_t count) {
        if (count > remaining_.size()) invalid();
        const auto bytes = remaining_.substr(0, count);
        remaining_.remove_prefix(count);
        return bytes;
    }

    std::uint64_t little(unsigned width) {
        std::uint64_t value = 0;
        unsigned shift = 0;
        for (unsigned char byte : take(width)) {
            value |= std::uint64_t(byte) << shift;
            shift += 8;
        }
        return value;
    }

    Tag type() {
        const auto value = little(1);
        if (value > static_cast<unsigned>(Tag::LongArray)) invalid();
        return static_cast<Tag>(value);
    }

    std::size_t length() {
        const auto count = little(4);
        if (count > INT32_MAX) invalid();
        return static_cast<std::size_t>(count);
    }

    std::string_view string() { return take(little(2)); }

    static Scope field_scope(Scope scope, std::string_view name, Tag tag) {
        if (scope == Scope::Root && name == "data") {
            if (tag != Tag::Compound) invalid();
            return Scope::Data;
        }
        if (scope == Scope::Data && name == "LimboEntities") {
            if (tag != Tag::List) invalid();
            return Scope::Limbo;
        }
        if (scope == Scope::Chunk && name == "EntityTagList") {
            if (tag != Tag::List) invalid();
            return Scope::Entities;
        }
        return Scope::Ignore;
    }

    void compound(Scope scope, unsigned depth) {
        for (auto tag = type(); tag != Tag::End; tag = type()) {
            const auto name = string();
            if (scope == Scope::Entity && name == "Variant" && tag >= Tag::Byte && tag <= Tag::Long) {
                const auto value = little(scalar_widths[static_cast<unsigned>(tag)]);
                for (std::size_t i = 0; i < targets.size(); ++i) {
                    if (value == targets[i]) matches_[i] = true;
                }
            } else {
                payload(tag, field_scope(scope, name, tag), depth + 1);
            }
        }
    }

    void list(Scope scope, unsigned depth) {
        const auto tag = type();
        const auto count = length();
        if (count == 0) return;
        if (tag == Tag::End || count > remaining_.size()) invalid();

        Scope entry_scope = Scope::Ignore;
        if (scope == Scope::Limbo) entry_scope = Scope::Chunk;
        if (scope == Scope::Entities) entry_scope = Scope::Entity;
        if (entry_scope != Scope::Ignore && tag != Tag::Compound) invalid();

        for (std::size_t i = 0; i < count; ++i) payload(tag, entry_scope, depth + 1);
    }

    void payload(Tag tag, Scope scope, unsigned depth) {
        if (depth > 512) invalid();
        switch (tag) {
            case Tag::Byte:
            case Tag::Short:
            case Tag::Int:
            case Tag::Long:
            case Tag::Float:
            case Tag::Double:
                take(scalar_widths[static_cast<unsigned>(tag)]);
                break;
            case Tag::ByteArray: take(length()); break;
            case Tag::String: string(); break;
            case Tag::List: list(scope, depth); break;
            case Tag::Compound: compound(scope, depth); break;
            case Tag::IntArray: take(length() * 4); break;
            case Tag::LongArray: take(length() * 8); break;
            case Tag::End: invalid();
        }
    }
};

class DatabaseCopy {
public:
    DatabaseCopy() : path((fs::temp_directory_path() / "endmetadata.XXXXXX").string()) {
        if (!mkdtemp(path.data())) throw std::runtime_error("Could not create temporary directory");
    }

    ~DatabaseCopy() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    DatabaseCopy(const DatabaseCopy&) = delete;
    DatabaseCopy& operator=(const DatabaseCopy&) = delete;

    std::string path;

    void copy_from(const fs::path& source) const {
        for (const auto& file : fs::directory_iterator(source)) {
            const auto name = file.path().filename();
            std::uint64_t number;
            leveldb::FileType type;
            if (!leveldb::ParseFileName(name.string(), &number, &type)) continue;
            if (type == leveldb::kDBLockFile || type == leveldb::kInfoLogFile || type == leveldb::kTempFile) continue;
            if (!fs::is_regular_file(file.symlink_status())) throw std::runtime_error("Invalid database file");
            const auto destination = fs::path(path) / name;
#ifdef __APPLE__
            if (copyfile(file.path().c_str(), destination.c_str(), nullptr, COPYFILE_DATA | COPYFILE_CLONE) == 0) continue;
#endif
            fs::copy_file(file.path(), destination, fs::copy_options::overwrite_existing);
        }
    }
};

Matches query(const fs::path& world) {
    // LevelDB may replay logs and update its manifest while opening the copy.
    DatabaseCopy copy;
    copy.copy_from(world / "db");

    leveldb::ZlibCompressorRaw raw;
    leveldb::ZlibCompressor zlib;
    leveldb::Options options;
    options.paranoid_checks = true;
    options.compressors[0] = &raw;
    options.compressors[1] = &zlib;
    leveldb::DB* opened = nullptr;
    const auto open_status = leveldb::DB::Open(options, copy.path, &opened);
    if (!open_status.ok()) throw std::runtime_error(open_status.ToString());
    const std::unique_ptr<leveldb::DB> db(opened);

    leveldb::ReadOptions read_options;
    read_options.verify_checksums = true;
    std::string metadata;
    const auto status = db->Get(read_options, "TheEnd", &metadata);
    if (status.IsNotFound()) return {};
    if (!status.ok()) throw std::runtime_error(status.ToString());
    return EndMetadata(metadata).read();
}
}  // namespace

int main(int argc, char** argv) {
    Matches matches{};
    int status = 0;
    if (argc > 2) {
        status = 2;
    } else {
        try {
            matches = query(argc == 2 ? argv[1] : "/var/mobile/Documents/My World");
        } catch (const std::exception&) {
            status = 1;
        }
    }
    for (std::size_t i = 0; i < targets.size(); ++i) {
        std::printf("\x1b[%dm%u\x1b[0m\n", matches[i] ? 32 : 31, targets[i]);
    }
    return status;
}
