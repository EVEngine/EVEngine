#include "asset/import/UnitySourceInternal.h"

#include <zlib.h>
#include <array>
#include <limits>
#include <set>

namespace eve::asset_import {
namespace {
using namespace unity_detail;

Result<std::vector<std::uint8_t>> inflatePackage(std::span<const std::uint8_t> input, const AssetImportLimits& limits) {
    if (input.empty() || input.size() > limits.maximumSourceBytes)
        return failure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                  "Unity package compressed size exceeds budget or is empty");
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        return failure<std::vector<std::uint8_t>>(DiagnosticCode::Failed, "cannot initialize Unity gzip decoder");
    struct EndInflate {
        z_stream& stream;
        ~EndInflate() { inflateEnd(&stream); }
    } cleanup{stream};
    std::vector<std::uint8_t>       out;
    std::array<std::uint8_t, 65536> buffer{};
    std::size_t                     cursor = 0;
    for (;;) {
        if (stream.avail_in == 0 && cursor < input.size()) {
            const auto count = std::min(input.size() - cursor, std::size_t(std::numeric_limits<uInt>::max()));
            stream.next_in   = const_cast<Bytef*>(input.data() + cursor);
            stream.avail_in  = static_cast<uInt>(count);
            cursor += count;
        }
        stream.next_out     = buffer.data();
        stream.avail_out    = static_cast<uInt>(buffer.size());
        const int  code     = inflate(&stream, Z_NO_FLUSH);
        const auto produced = buffer.size() - stream.avail_out;
        if (produced > limits.maximumDecodedBytes || out.size() > limits.maximumDecodedBytes - produced)
            return failure<std::vector<std::uint8_t>>(DiagnosticCode::InvalidArgument,
                                                      "Unity package decompressed size exceeds budget");
        out.insert(out.end(), buffer.begin(), buffer.begin() + produced);
        if (code == Z_STREAM_END) {
            if (stream.avail_in != 0 || cursor != input.size())
                return failure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                          "Unity package has trailing or concatenated gzip data");
            return Result<std::vector<std::uint8_t>>::success(std::move(out));
        }
        if (code != Z_OK || (produced == 0 && stream.avail_in == 0 && cursor == input.size()))
            return failure<std::vector<std::uint8_t>>(DiagnosticCode::ParseError,
                                                      "Unity package gzip is truncated or corrupt");
    }
}

std::string tarString(std::span<const std::uint8_t> bytes) {
    const auto end = std::find(bytes.begin(), bytes.end(), 0);
    return {bytes.begin(), end};
}

Result<std::uint64_t> octal(std::span<const std::uint8_t> bytes) {
    std::uint64_t value = 0;
    bool          ended = false, digit = false;
    for (auto byte : bytes) {
        if (byte == 0 || byte == ' ') {
            if (digit || byte == 0) ended = true;
            continue;
        }
        if (ended || byte < '0' || byte > '7' || value > (std::numeric_limits<std::uint64_t>::max() - (byte - '0')) / 8)
            return failure<std::uint64_t>(DiagnosticCode::ParseError, "invalid Unity tar numeric field");
        digit = true;
        value = value * 8 + (byte - '0');
    }
    return Result<std::uint64_t>::success(value);
}

struct PackageAsset {
    std::map<std::string, std::span<const std::uint8_t>> members;
};

Result<UnitySourceFiles> unpack(std::span<const std::uint8_t> tar, const AssetImportLimits& limits) {
    if (tar.size() < 1024 || tar.size() % 512 != 0)
        return failure<UnitySourceFiles>(DiagnosticCode::ParseError, "Unity tar block size is invalid");
    std::map<std::string, PackageAsset> assets;
    std::set<std::string>               memberNames;
    bool                                ended = false;
    for (std::size_t cursor = 0; cursor < tar.size();) {
        const auto header = tar.subspan(cursor, 512);
        if (std::all_of(header.begin(), header.end(), [](auto b) { return b == 0; })) {
            if (tar.size() - cursor < 1024 ||
                !std::all_of(tar.begin() + cursor, tar.end(), [](auto b) { return b == 0; }))
                return failure<UnitySourceFiles>(DiagnosticCode::ParseError, "Unity tar termination is invalid");
            ended = true;
            break;
        }
        auto storedSum = octal(header.subspan(148, 8));
        if (!storedSum) return Result<UnitySourceFiles>::failure(storedSum.status());
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < 512; ++i) sum += i >= 148 && i < 156 ? ' ' : header[i];
        if (sum != storedSum.value())
            return failure<UnitySourceFiles>(DiagnosticCode::ParseError, "Unity tar checksum mismatch");
        auto sizeResult = octal(header.subspan(124, 12));
        if (!sizeResult) return Result<UnitySourceFiles>::failure(sizeResult.status());
        const auto size = sizeResult.value();
        cursor += 512;
        if (size > limits.maximumSourceBytes || size > tar.size() - cursor)
            return failure<UnitySourceFiles>(DiagnosticCode::InvalidArgument, "Unity tar entry size exceeds bounds");
        const auto payload = tar.subspan(cursor, static_cast<std::size_t>(size));
        const auto padding = (512 - size % 512) % 512;
        if (size + padding > tar.size() - cursor)
            return failure<UnitySourceFiles>(DiagnosticCode::ParseError, "Unity tar padding is truncated");
        cursor += static_cast<std::size_t>(size + padding);
        auto name = tarString(header.first(100));
        if (tarString(header.subspan(257, 6)) == "ustar") {
            const auto prefix = tarString(header.subspan(345, 155));
            if (!prefix.empty()) name = prefix + "/" + name;
        }
        if (name.starts_with("./")) name.erase(0, 2);
        const auto type = header[156];
        if (type != 0 && type != '0' && type != '5')
            return failure<UnitySourceFiles>(DiagnosticCode::Unsupported,
                                             "Unity tar links and extended entry types are not accepted", name);
        if (type == '5' && name.ends_with('/')) name.pop_back();
        if (type == '5' && (name.empty() || name == ".")) continue;
        if (!validPath(name, limits) || !memberNames.insert(name).second)
            return failure<UnitySourceFiles>(DiagnosticCode::Conflict, "unsafe or duplicate Unity tar path", name);
        if (memberNames.size() > std::uint64_t(limits.maximumAssets) * 6)
            return failure<UnitySourceFiles>(DiagnosticCode::InvalidArgument, "Unity tar entry count exceeds budget");
        // Package thumbnail, like per-asset preview.png, is not an import source.
        if (name == ".icon.png" && type != '5') continue;
        const auto slash = name.find('/');
        const auto guid  = foldAscii(name.substr(0, slash));
        if (!validGuid(guid) && guid != "packagemanagermanifest")
            return failure<UnitySourceFiles>(DiagnosticCode::ParseError, "Unity tar entry needs a GUID directory",
                                             name);
        if (type == '5') {
            if (slash != std::string::npos || size != 0)
                return failure<UnitySourceFiles>(DiagnosticCode::ParseError, "invalid Unity tar directory", name);
            continue;
        }
        if (slash == std::string::npos)
            return failure<UnitySourceFiles>(DiagnosticCode::ParseError, "Unity tar entry has no member name", name);
        const auto member = name.substr(slash + 1);
        if (member != "asset" && member != "asset.meta" && member != "pathname" && member != "preview.png")
            return failure<UnitySourceFiles>(DiagnosticCode::Unsupported, "unknown Unity package member", name);
        if (!assets[guid].members.emplace(member, payload).second)
            return failure<UnitySourceFiles>(DiagnosticCode::Conflict, "duplicate Unity package member", name);
        if (assets.size() > limits.maximumAssets)
            return failure<UnitySourceFiles>(DiagnosticCode::InvalidArgument,
                                             "Unity package asset count exceeds budget");
    }
    if (!ended || assets.empty())
        return failure<UnitySourceFiles>(DiagnosticCode::ParseError, "Unity tar is empty or unterminated");
    UnitySourceFiles      files;
    std::set<std::string> paths;
    for (const auto& [guid, entry] : assets) {
        const auto pathname = entry.members.find("pathname"), meta = entry.members.find("asset.meta");
        const bool dependencyManifest = guid == "packagemanagermanifest";
        if (pathname == entry.members.end() || (!dependencyManifest && meta == entry.members.end()))
            return failure<UnitySourceFiles>(DiagnosticCode::NotFound, "Unity package needs pathname and asset.meta",
                                             guid);
        if (pathname->second.size() > limits.maximumStringBytes + std::uint64_t(4))
            return failure<UnitySourceFiles>(DiagnosticCode::InvalidArgument, "Unity pathname exceeds budget", guid);
        std::string path(pathname->second.begin(), pathname->second.end());
        // Legacy Asset Store exports append this exact trailer to the pathname record.
        if (path.ends_with("\n00"))
            path.resize(path.size() - 3);
        else if (path.ends_with('\n'))
            path.pop_back();
        if (path.ends_with('\r')) path.pop_back();
        if (dependencyManifest) {
            const auto asset = entry.members.find("asset");
            if (!validPath(path, limits) || path != "Packages/manifest.json" || asset == entry.members.end() ||
                entry.members.size() != 2)
                return failure<UnitySourceFiles>(DiagnosticCode::ParseError,
                                                 "invalid Unity package dependency manifest", path);
            // Preserve as source data; it never authorizes package installation.
            files.emplace(path, std::vector<std::uint8_t>(asset->second.begin(), asset->second.end()));
            continue;
        }
        if (!validPath(path, limits) || !(path == "Assets" || path.starts_with("Assets/")))
            return failure<UnitySourceFiles>(DiagnosticCode::InvalidArgument, "Unity pathname must be inside Assets",
                                             path);
        for (const auto& candidate : {path, path + ".meta"})
            if (!paths.insert(foldAscii(candidate)).second)
                return failure<UnitySourceFiles>(DiagnosticCode::Conflict, "duplicate Unity destination path", path);
        auto metaGuid = metaScalar(meta->second, "guid", path);
        if (!metaGuid) return Result<UnitySourceFiles>::failure(metaGuid.status());
        if (foldAscii(metaGuid.value()) != guid)
            return failure<UnitySourceFiles>(DiagnosticCode::Conflict, "Unity directory GUID disagrees with .meta",
                                             path);
        files.emplace(path + ".meta", std::vector<std::uint8_t>(meta->second.begin(), meta->second.end()));
        const auto asset = entry.members.find("asset");
        if (asset != entry.members.end())
            files.emplace(path, std::vector<std::uint8_t>(asset->second.begin(), asset->second.end()));
    }
    auto index = indexUnitySources(files, limits);
    if (!index) return Result<UnitySourceFiles>::failure(index.status());
    return Result<UnitySourceFiles>::success(std::move(files));
}
}  // namespace

Result<UnitySourceFiles> readUnityPackage(std::span<const std::uint8_t> bytes, const AssetImportLimits& limits) {
    auto decoded = inflatePackage(bytes, limits);
    if (!decoded) return Result<UnitySourceFiles>::failure(decoded.status());
    return unpack(decoded.value(), limits);
}
}  // namespace eve::asset_import
