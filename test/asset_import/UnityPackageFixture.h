#pragma once

#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>
#include "zeroerr/assert.h"

namespace unity_test {
constexpr std::string_view guidA = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
constexpr std::string_view guidB = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

inline std::vector<std::uint8_t> bytes(std::string_view text) { return {text.begin(), text.end()}; }

inline std::vector<std::uint8_t> metadata(std::string_view guid, std::string_view extra = {}) {
    return bytes("fileFormatVersion: 2\nguid: " + std::string(guid) + "\n" + std::string(extra));
}

inline void appendTar(std::vector<std::uint8_t>& out, std::string_view name, const std::vector<std::uint8_t>& payload,
                      char type = '0') {
    std::array<char, 512> header{};
    REQUIRE(name.size() < 100);
    std::copy(name.begin(), name.end(), header.begin());
    std::snprintf(header.data() + 100, 8, "%07o", 0644);
    std::snprintf(header.data() + 124, 12, "%011llo", static_cast<unsigned long long>(payload.size()));
    header[156] = type;
    std::copy_n("ustar", 5, header.data() + 257);
    std::fill(header.begin() + 148, header.begin() + 156, ' ');
    unsigned sum = 0;
    for (unsigned char c : header) sum += c;
    std::snprintf(header.data() + 148, 7, "%06o", sum);
    out.insert(out.end(), header.begin(), header.end());
    out.insert(out.end(), payload.begin(), payload.end());
    out.resize((out.size() + 511) / 512 * 512);
}

inline void appendAsset(std::vector<std::uint8_t>& tar, std::string_view guid, std::string_view path,
                        std::string_view payload = "%YAML 1.1\n") {
    const std::string root = "./" + std::string(guid) + "/";
    appendTar(tar, root, {}, '5');
    appendTar(tar, root + "asset", bytes(payload));
    appendTar(tar, root + "asset.meta", metadata(guid));
    appendTar(tar, root + "pathname", bytes(path));
}

inline std::vector<std::uint8_t> gzip(const std::vector<std::uint8_t>& tar) {
    z_stream  stream{};
    const int initialized =
        deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    REQUIRE_EQ(initialized, Z_OK);
    std::vector<std::uint8_t> out(compressBound(static_cast<uLong>(tar.size())) + 32);
    stream.next_in    = const_cast<Bytef*>(tar.data());
    stream.avail_in   = static_cast<uInt>(tar.size());
    stream.next_out   = out.data();
    stream.avail_out  = static_cast<uInt>(out.size());
    const int  result = deflate(&stream, Z_FINISH);
    const auto size   = stream.total_out;
    const int  ended  = deflateEnd(&stream);
    REQUIRE_EQ(ended, Z_OK);
    REQUIRE_EQ(result, Z_STREAM_END);
    out.resize(size);
    return out;
}
}  // namespace unity_test
