#pragma once

// Minimal streaming ZIP writer used by `eve zip` / `eve package` to produce a
// PhysFS-mountable game archive (.eve). Entries are DEFLATE-compressed (method 8)
// using zlib's raw deflate. The output is a standard ZIP readable by PhysFS.

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <zlib.h>

namespace eve::cmdline {

// 0 = STORED (no compression), 8 = DEFLATE.
#define EVE_ZIP_METHOD 8

namespace detail {

// Raw deflate (no zlib/gzip wrapper) 鈥?required for ZIP method 8 entries.
inline bool rawDeflate(const char* in, size_t inLen, std::vector<char>& out) {
    z_stream stream = {};
    stream.next_in  = reinterpret_cast<Bytef*>(const_cast<char*>(in));
    stream.avail_in = static_cast<uInt>(inLen);

    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;

    out.resize(inLen > 0 ? static_cast<size_t>(deflateBound(&stream, inLen)) : 1024);

    stream.next_out  = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());

    int ret = deflate(&stream, Z_FINISH);
    bool ok = (ret == Z_STREAM_END);
    deflateEnd(&stream);

    if (!ok) return false;
    out.resize(stream.total_out);
    return true;
}

inline void put16(std::vector<char>& b, uint16_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>((v >> 8) & 0xFF));
}
inline void put32(std::vector<char>& b, uint32_t v) {
    b.push_back(static_cast<char>(v & 0xFF));
    b.push_back(static_cast<char>((v >> 8) & 0xFF));
    b.push_back(static_cast<char>((v >> 16) & 0xFF));
    b.push_back(static_cast<char>((v >> 24) & 0xFF));
}

}  // namespace detail

class ZipWriter {
public:
    // Open the destination archive. Must be called before any addFile().
    bool open(const std::string& outPath) {
        outFile_.open(outPath, std::ios::binary | std::ios::trunc);
        return outFile_.is_open();
    }

    // Add a regular file. relPath uses '/' separators, relative to the archive root.
    bool addFile(const std::string& relPath, const char* data, size_t size) {
        const uint32_t localOffset = static_cast<uint32_t>(offset_);
        uint32_t crc = static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(data),
                                                   static_cast<uInt>(size)));

        std::vector<char> compressed;
#if EVE_ZIP_METHOD == 8
        if (!detail::rawDeflate(data, size, compressed)) return false;
#else
        compressed.assign(data, data + size);
#endif

        std::vector<char> lfh;
        lfh.reserve(30 + relPath.size());
        detail::put32(lfh, 0x04034B50);            // local file header signature
        detail::put16(lfh, 20);                    // version needed
        detail::put16(lfh, 0);                     // general purpose bit flag
        detail::put16(lfh, EVE_ZIP_METHOD);        // compression method
        detail::put16(lfh, 0);                     // last mod time
        detail::put16(lfh, 0x21);                  // last mod date (1980-01-01)
        detail::put32(lfh, crc);
        detail::put32(lfh, static_cast<uint32_t>(compressed.size()));
        detail::put32(lfh, static_cast<uint32_t>(size));
        detail::put16(lfh, static_cast<uint16_t>(relPath.size()));
        detail::put16(lfh, 0);                     // extra field length
        lfh.insert(lfh.end(), relPath.begin(), relPath.end());

        if (!writeRaw(lfh)) return false;
        if (!writeRaw(compressed)) return false;

        CentralEntry e;
        e.relPath        = relPath;
        e.crc            = crc;
        e.compressedSz   = static_cast<uint32_t>(compressed.size());
        e.uncompressedSz = static_cast<uint32_t>(size);
        e.localOffset    = localOffset;
        entries_.push_back(std::move(e));

        return true;
    }

    // Write the central directory + end-of-central-directory and close.
    bool finish() {
        if (!outFile_.is_open()) return false;
        const uint32_t cdStart = static_cast<uint32_t>(offset_);
        for (const auto& e : entries_) {
            std::vector<char> cd;
            cd.reserve(46 + e.relPath.size());
            detail::put32(cd, 0x02014B50);         // central directory signature
            detail::put16(cd, 20);                 // version made by
            detail::put16(cd, 20);                 // version needed
            detail::put16(cd, 0);                  // flags
            detail::put16(cd, EVE_ZIP_METHOD);     // method
            detail::put16(cd, 0);                  // mod time
            detail::put16(cd, 0x21);               // mod date
            detail::put32(cd, e.crc);
            detail::put32(cd, e.compressedSz);
            detail::put32(cd, e.uncompressedSz);
            detail::put16(cd, static_cast<uint16_t>(e.relPath.size()));
            detail::put16(cd, 0);                  // extra
            detail::put16(cd, 0);                  // comment
            detail::put16(cd, 0);                  // disk number
            detail::put16(cd, 0);                  // internal attrs
            detail::put32(cd, 0);                  // external attrs
            detail::put32(cd, e.localOffset);
            cd.insert(cd.end(), e.relPath.begin(), e.relPath.end());

            if (!writeRaw(cd)) return false;
        }
        const uint32_t cdEnd = static_cast<uint32_t>(offset_);

        std::vector<char> eocd;
        detail::put32(eocd, 0x06054B50);           // end of central directory
        detail::put16(eocd, 0);                    // disk number
        detail::put16(eocd, 0);                    // cd start disk
        detail::put16(eocd, static_cast<uint16_t>(entries_.size()));
        detail::put16(eocd, static_cast<uint16_t>(entries_.size()));
        detail::put32(eocd, cdEnd - cdStart);
        detail::put32(eocd, cdStart);
        detail::put16(eocd, 0);                    // comment length
        if (!writeRaw(eocd)) return false;

        outFile_.close();
        return true;
    }

private:
    struct CentralEntry {
        std::string relPath;
        uint32_t    crc = 0;
        uint32_t    compressedSz = 0;
        uint32_t    uncompressedSz = 0;
        uint32_t    localOffset = 0;
    };

    bool writeRaw(const std::vector<char>& data) {
        if (!outFile_.is_open()) return false;
        outFile_.write(data.data(), static_cast<std::streamsize>(data.size()));
        offset_ += data.size();
        return static_cast<bool>(outFile_);
    }

    std::ofstream outFile_;
    uint64_t      offset_ = 0;
    std::vector<CentralEntry> entries_;
};

    // Walk a game directory and write every regular file into a single .eve archive.
    inline bool createGameArchive(const std::filesystem::path& gameDir,
                                  const std::filesystem::path& outArchive) {
        std::error_code ec;
        if (!std::filesystem::is_directory(gameDir, ec)) return false;

        ZipWriter writer;
        if (!writer.open(outArchive.string())) return false;

        // Recursively collect files, preserving relative paths with '/' separators.
        std::vector<std::filesystem::path> files;
        std::filesystem::recursive_directory_iterator it(
            gameDir, std::filesystem::directory_options::skip_permission_denied, ec);
        std::filesystem::recursive_directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (it->is_regular_file()) files.push_back(it->path());
        }

        for (const auto& file : files) {
            std::string rel = std::filesystem::relative(file, gameDir, ec).generic_string();
            if (rel.empty()) continue;
            // Always use '/' inside the archive.
            for (char& c : rel) if (c == '\\') c = '/';

            std::ifstream in(file, std::ios::binary);
            if (!in) continue;
            in.seekg(0, std::ios::end);
            const auto size = static_cast<size_t>(in.tellg());
            in.seekg(0, std::ios::beg);

            std::vector<char> buf(size);
            if (size > 0) in.read(buf.data(), static_cast<std::streamsize>(size));
            if (!in && size > 0) continue;

            if (!writer.addFile(rel, buf.data(), buf.size())) return false;
        }

        return writer.finish();
    }

}  // namespace eve::cmdline

