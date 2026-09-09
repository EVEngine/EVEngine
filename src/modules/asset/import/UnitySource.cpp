#include "asset/import/UnitySourceInternal.h"

#include <charconv>
#include <regex>
#include <set>
#include <tuple>

namespace eve::asset_import::unity_detail {

Result<std::string> metaScalar(std::span<const std::uint8_t> bytes, std::string_view key, std::string_view path) {
    if (std::find(bytes.begin(), bytes.end(), 0) != bytes.end())
        return failure<std::string>(DiagnosticCode::ParseError, "binary Unity metadata", std::string(path));
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (!isValidUtf8(text, Utf8NullPolicy::Reject))
        return failure<std::string>(DiagnosticCode::ParseError, "Unity metadata is not UTF-8", std::string(path));
    std::string value;
    bool        found = false;
    for (std::size_t at = 0; at < text.size();) {
        auto end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(at, end - at);
        if (at == 0 && line.starts_with("\xef\xbb\xbf")) line.remove_prefix(3);
        if (line.starts_with(key) && line.size() > key.size() && line[key.size()] == ':') {
            if (found)
                return failure<std::string>(DiagnosticCode::Conflict,
                                            "duplicate Unity metadata key: " + std::string(key), std::string(path));
            found = true;
            line.remove_prefix(key.size() + 1);
            const auto first = line.find_first_not_of(" \t\r");
            if (first != std::string_view::npos)
                value = std::string(line.substr(first, line.find_last_not_of(" \t\r") - first + 1));
        }
        at = end + 1;
    }
    return Result<std::string>::success(std::move(value));
}

}  // namespace eve::asset_import::unity_detail

namespace eve::asset_import {
namespace {
using namespace unity_detail;

UnitySourceKind classify(std::string_view path) {
    const auto dot = path.find_last_of('.');
    const auto ext = dot == std::string_view::npos ? std::string{} : foldAscii(std::string(path.substr(dot)));
    if (ext == ".prefab") return UnitySourceKind::Prefab;
    if (ext == ".unity") return UnitySourceKind::Scene;
    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".blend" || ext == ".dae")
        return UnitySourceKind::Model;
    if (ext == ".mat") return UnitySourceKind::Material;
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".psd" || ext == ".exr" ||
        ext == ".tif" || ext == ".tiff" || ext == ".bmp")
        return UnitySourceKind::Image;
    if (ext == ".anim" || ext == ".controller" || ext == ".overridecontroller") return UnitySourceKind::Animation;
    if (ext == ".wav" || ext == ".ogg" || ext == ".mp3" || ext == ".aiff" || ext == ".flac")
        return UnitySourceKind::Audio;
    if (ext == ".ttf" || ext == ".otf") return UnitySourceKind::Font;
    if (ext == ".shader" || ext == ".shadergraph" || ext == ".shadersubgraph" || ext == ".compute")
        return UnitySourceKind::Shader;
    if (ext == ".cs" || ext == ".dll" || ext == ".asmdef") return UnitySourceKind::Script;
    if (ext == ".asset" || ext == ".terrainlayer") return UnitySourceKind::Data;
    return UnitySourceKind::Other;
}

bool builtin(std::string_view guid) {
    return guid == "00000000000000000000000000000000" || guid == "0000000000000000e000000000000000" ||
           guid == "0000000000000000f000000000000000";
}

Result<std::vector<UnitySourceReference>> references(std::span<const std::uint8_t> bytes, const std::string& path,
                                                     const AssetImportLimits& limits) {
    std::vector<UnitySourceReference> out;
    if (bytes.empty() || std::find(bytes.begin(), bytes.end(), 0) != bytes.end())
        return Result<std::vector<UnitySourceReference>>::success(std::move(out));
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const std::regex       guidField(R"(\bguid:\s*([^,}\s]+))");
    const std::regex       idField(R"(\bfileID:\s*([^,}\s]+))");
    for (std::size_t at = 0; at < text.size();) {
        const auto begin = text.find('{', at);
        if (begin == std::string_view::npos) break;
        const auto end = text.find_first_of("{}\r\n", begin + 1);
        if (end == std::string_view::npos) break;
        at = text[end] == '{' ? end : end + 1;
        if (text[end] != '}') continue;
        if (end - begin > limits.maximumStringBytes)
            return failure<std::vector<UnitySourceReference>>(
                DiagnosticCode::InvalidArgument, "Unity serialized flow record exceeds string budget", path);
        const std::string ref(text.substr(begin, end - begin + 1));
        std::smatch       guid, id;
        if (!std::regex_search(ref, guid, guidField)) continue;
        if (!validGuid(guid[1].str()) || !std::regex_search(ref, id, idField))
            return failure<std::vector<UnitySourceReference>>(DiagnosticCode::ParseError,
                                                              "malformed Unity object reference", path);
        const auto   idText = id[1].str();
        std::int64_t fileId = 0;
        auto         parsed = std::from_chars(idText.data(), idText.data() + idText.size(), fileId);
        if (parsed.ec != std::errc{} || parsed.ptr != idText.data() + idText.size())
            return failure<std::vector<UnitySourceReference>>(
                DiagnosticCode::ParseError, "Unity reference fileID exceeds signed 64-bit range", path);
        if (out.size() >= limits.maximumAssets)
            return failure<std::vector<UnitySourceReference>>(DiagnosticCode::InvalidArgument,
                                                              "Unity reference count exceeds budget", path);
        out.push_back({foldAscii(guid[1].str()), fileId});
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return std::tie(a.guid, a.fileId) < std::tie(b.guid, b.fileId); });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const auto& a, const auto& b) { return a.guid == b.guid && a.fileId == b.fileId; }),
              out.end());
    return Result<std::vector<UnitySourceReference>>::success(std::move(out));
}
}  // namespace

Result<UnitySourceIndex> indexUnitySources(const UnitySourceFiles& files, const AssetImportLimits& limits) {
    if (files.empty() || files.size() > std::uint64_t(limits.maximumAssets) * 2)
        return failure<UnitySourceIndex>(DiagnosticCode::InvalidArgument,
                                         "Unity source count exceeds budget or is empty");
    std::uint64_t         total = 0;
    std::set<std::string> paths;
    for (const auto& [path, bytes] : files) {
        if (!validPath(path, limits) || bytes.size() > limits.maximumSourceBytes ||
            total > limits.maximumSourceBytes - bytes.size())
            return failure<UnitySourceIndex>(DiagnosticCode::InvalidArgument, "Unity path or source budget is invalid",
                                             path);
        total += bytes.size();
        if (!paths.insert(foldAscii(path)).second)
            return failure<UnitySourceIndex>(DiagnosticCode::Conflict, "Unity source paths differ only by case", path);
    }
    UnitySourceIndex                   index;
    std::map<std::string, std::string> guidPaths;
    std::map<std::string, std::size_t> assetPaths;
    for (const auto& [path, bytes] : files) {
        if (path.ends_with(".meta")) continue;
        assetPaths.emplace(path, index.assets.size());
        index.assets.push_back({path, {}, classify(path), {}, {}});
    }
    for (const auto& [path, bytes] : files) {
        if (!path.ends_with(".meta")) continue;
        const std::string sourcePath = path.substr(0, path.size() - 5);
        auto              guid       = metaScalar(bytes, "guid", path);
        if (!guid) return Result<UnitySourceIndex>::failure(guid.status());
        if (!validGuid(guid.value()) || builtin(foldAscii(guid.value())))
            return failure<UnitySourceIndex>(DiagnosticCode::ParseError, "Unity .meta needs a non-reserved GUID", path);
        const auto normalizedGuid = foldAscii(guid.value());
        if (!guidPaths.emplace(normalizedGuid, sourcePath).second)
            return failure<UnitySourceIndex>(DiagnosticCode::Conflict, "duplicate Unity GUID", path);
        auto folder = metaScalar(bytes, "folderAsset", path);
        if (!folder) return Result<UnitySourceIndex>::failure(folder.status());
        const auto found = assetPaths.find(sourcePath);
        if (folder.value() == "yes") {
            if (found != assetPaths.end())
                return failure<UnitySourceIndex>(DiagnosticCode::Conflict, "Unity folder also contains a file payload",
                                                 path);
            index.assets.push_back({sourcePath, normalizedGuid, UnitySourceKind::Folder, {}, {}});
            continue;
        }
        if (found == assetPaths.end())
            return failure<UnitySourceIndex>(DiagnosticCode::NotFound, "Unity metadata has no source payload", path);
        auto& source = index.assets[found->second];
        source.guid  = normalizedGuid;
        const std::string_view meta(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        for (std::size_t at = 0; at < meta.size();) {
            auto end = meta.find('\n', at);
            if (end == std::string_view::npos) end = meta.size();
            auto line = meta.substr(at, end - at);
            if (line.ends_with('\r')) line.remove_suffix(1);
            if (line.ends_with("Importer:") && line.find_first_of(" \t") == std::string_view::npos)
                source.importer = std::string(line.substr(0, line.size() - 1));
            at = end + 1;
        }
        auto refs = references(bytes, path, limits);
        if (!refs) return Result<UnitySourceIndex>::failure(refs.status());
        source.references = std::move(refs).takeValue();
    }
    if (index.assets.size() > limits.maximumAssets)
        return failure<UnitySourceIndex>(DiagnosticCode::InvalidArgument, "Unity asset count exceeds budget");
    std::map<std::string, UnitySourceKind> sourcePaths;
    for (const auto& entry : index.assets) {
        if (!sourcePaths.emplace(foldAscii(entry.path), entry.kind).second)
            return failure<UnitySourceIndex>(DiagnosticCode::Conflict, "Unity source identities share a path",
                                             entry.path);
    }
    for (const auto& entry : index.assets) {
        for (auto slash = entry.path.find('/'); slash != std::string::npos; slash = entry.path.find('/', slash + 1)) {
            const auto parent = sourcePaths.find(foldAscii(entry.path.substr(0, slash)));
            if (parent != sourcePaths.end() && parent->second != UnitySourceKind::Folder)
                return failure<UnitySourceIndex>(DiagnosticCode::Conflict,
                                                 "Unity source file is also used as a directory", entry.path);
        }
    }
    std::sort(index.assets.begin(), index.assets.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    for (auto& entry : index.assets) {
        if (entry.kind == UnitySourceKind::Folder) continue;
        if (entry.guid.empty())
            index.findings.push_back({entry.path, "identity.meta", ImportDisposition::Unsupported,
                                      "source has no .meta GUID; stable Unity reimport identity is unavailable"});
        if (entry.kind == UnitySourceKind::Prefab || entry.kind == UnitySourceKind::Scene ||
            entry.kind == UnitySourceKind::Material || entry.kind == UnitySourceKind::Data ||
            entry.kind == UnitySourceKind::Animation) {
            auto refs = references(files.at(entry.path), entry.path, limits);
            if (!refs) return Result<UnitySourceIndex>::failure(refs.status());
            for (auto& ref : refs.value()) entry.references.push_back(std::move(ref));
        }
        for (const auto& ref : entry.references)
            if (ref.fileId != 0 && !builtin(ref.guid) && !guidPaths.contains(ref.guid))
                index.findings.push_back({entry.path, "dependency:" + ref.guid + ":" + std::to_string(ref.fileId),
                                          ImportDisposition::Unsupported, "referenced Unity asset was not supplied"});
    }
    return Result<UnitySourceIndex>::success(std::move(index));
}
}  // namespace eve::asset_import
