#include <cmath>
#include <regex>
#include <set>
#include <stdexcept>
#include "asset/SpriteAnimation.h"
#include "asset/import/ImportCommon.h"
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"

namespace eve::asset_import {
namespace {
std::string capture(const std::string& text, const std::string& pattern) {
    std::smatch match;
    if (!std::regex_search(text, match, std::regex(pattern)))
        throw std::runtime_error("missing or unsupported sprite metadata: " + pattern);
    return match[1].str();
}
double scalar(const std::string& text, const std::string& pattern) {
    const auto   s    = capture(text, pattern);
    std::size_t  used = 0;
    const double n    = std::stod(s, &used);
    if (used != s.size() || !std::isfinite(n)) throw std::runtime_error("invalid sprite scalar");
    return n;
}
std::string contents(const UnityProjectImportRequest& request, const std::string& path) {
    const auto& b = request.files.at(path);
    return {b.begin(), b.end()};
}
std::uint32_t big32(const std::vector<std::uint8_t>& b, std::size_t i) {
    return (std::uint32_t(b[i]) << 24) | (std::uint32_t(b[i + 1]) << 16) | (std::uint32_t(b[i + 2]) << 8) | b[i + 3];
}
Value::Object sprite(const UnityProjectImportRequest& request, const UnitySourceAsset& source, const std::string& id) {
    const auto& png = request.files.at(source.path);
    if (png.size() < 24 || png[0] != 137 || std::string(png.begin() + 1, png.begin() + 4) != "PNG")
        throw std::runtime_error("sprite image must be PNG");
    const double width = big32(png, 16), height = big32(png, 20);
    const auto   meta = contents(request, source.path + ".meta");
    if (scalar(meta, R"(\n  spriteMode: ([^\r\n]+))") != 2)
        throw std::runtime_error("sprite clip requires explicit multiple-sprite slicing");
    const auto       sheet = capture(meta, R"(\n  spriteSheet:([\s\S]*))");
    const std::regex blocks(
        R"(\n    - serializedVersion: [^\n]+\n      name: ([^\r\n]+)\n([\s\S]*?)(?=\n    - serializedVersion:|\n    outline:|\n    spriteID:|\n    nameFileIdTable:|$))");
    std::optional<Value::Object> result;
    for (std::sregex_iterator it(sheet.begin(), sheet.end(), blocks), end; it != end; ++it) {
        const auto body = (*it)[2].str();
        if (capture(body, R"(\n      internalID: (-?[0-9]+))") != id) continue;
        if (result) throw std::runtime_error("duplicate sprite fileID");
        const double x = scalar(body, R"(\n        x: ([^\r\n]+))"), y = scalar(body, R"(\n        y: ([^\r\n]+))");
        const double w         = scalar(body, R"(\n        width: ([^\r\n]+))"),
                     h         = scalar(body, R"(\n        height: ([^\r\n]+))");
        const double alignment = scalar(body, R"(\n      alignment: ([^\r\n]+))");
        double       px = .5, py = .5;
        if (alignment == 9) {
            px = scalar(body, R"(\n      pivot: \{x: ([^,]+),)");
            py = scalar(body, R"(\n      pivot: \{x: [^,]+, y: ([^}]+)\})");
        } else if (alignment != 0)
            throw std::runtime_error("only centered/custom sprite pivots supported");
        const double filter = scalar(meta, R"(\n    filterMode: ([^\r\n]+))");
        if (filter != 0 && filter != 1) throw std::runtime_error("unsupported sprite filtering");
        if (!std::regex_search(body, std::regex(R"(border: \{x: 0, y: 0, z: 0, w: 0\})")))
            throw std::runtime_error("sprite borders require nine-slice conversion");
        const auto imageId = request.package.packageId.child("unity:" + source.guid).child("image:default");
        auto       ref     = detail::assetRef(imageId);
        if (!ref) throw std::runtime_error(ref.error()->message());
        result = Value::Object{{"image", ref.value().format()},
                               {"name", (*it)[1].str()},
                               {"rect", Value::Array{x, height - y - h, w, h}},
                               {"pivot", Value::Array{px, 1 - py}},
                               {"imageSize", Value::Array{width, height}},
                               {"pixelsPerUnit", scalar(meta, R"(\n  spritePixelsToUnits: ([^\r\n]+))")},
                               {"filter", filter == 0 ? "nearest" : "linear"},
                               {"sourceGuid", source.guid},
                               {"sourceFileId", id}};
    }
    if (!result) throw std::runtime_error("sprite fileID not found: " + source.guid + "/" + id);
    return std::move(*result);
}
}  // namespace
Result<PreparedAssetImport> prepareUnitySpriteAnimation(const UnityProjectImportRequest& request,
                                                        const UnitySourceAsset&          source) try {
    const auto text = contents(request, source.path);
    if (text.find("m_PPtrCurves:") == std::string::npos)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                    "animation has no supported sprite track", source.path);
    const auto curves = capture(text, R"(\n  m_PPtrCurves:([\s\S]*?)\n  m_SampleRate:)");
    if (!std::regex_search(curves,
                           std::regex(R"(\n    attribute: m_Sprite\r?\n    path: *\r?\n    classID: 212\r?\n)")))
        return detail::failure<PreparedAssetImport>(
            DiagnosticCode::Unsupported, "only root SpriteRenderer sprite curves are supported", source.path);
    for (const auto field : {"m_RotationCurves", "m_CompressedRotationCurves", "m_EulerCurves", "m_PositionCurves",
                             "m_ScaleCurves", "m_FloatCurves", "m_Events"})
        if (!std::regex_search(text, std::regex(std::string("\\n  ") + field + ": \\[\\]")))
            return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                        "clip contains non-sprite curves or events", source.path);
    const std::regex curveStart(R"(\n  - curve:)");
    if (std::distance(std::sregex_iterator(curves.begin(), curves.end(), curveStart), std::sregex_iterator{}) != 1)
        return detail::failure<PreparedAssetImport>(DiagnosticCode::Unsupported,
                                                    "multiple sprite tracks are unsupported", source.path);
    const double fps      = scalar(text, R"(\n  m_SampleRate: ([^\r\n]+))");
    const double duration = scalar(text, R"(\n    m_StopTime: ([^\r\n]+))");
    const double loop     = scalar(text, R"(\n    m_LoopTime: ([^\r\n]+))");
    if (fps <= 0 || fps > 1000 || (loop != 0 && loop != 1) || scalar(text, R"(\n    m_StartTime: ([^\r\n]+))") != 0)
        throw std::runtime_error("invalid sprite playback settings");
    auto indexed = indexUnitySources(request.files, request.limits);
    if (!indexed) return Result<PreparedAssetImport>::failure(indexed.status());
    std::map<std::string, UnitySourceAsset> sources;
    for (const auto& s : indexed.value().assets)
        if (!s.guid.empty()) sources.emplace(s.guid, s);
    Value::Array                         frames;
    std::set<std::string>                images;
    std::map<std::string, Value::Object> cache;
    const std::regex                     key(
        R"(\n    - time: ([^\r\n]+)\r?\n      value: \{fileID: (-?[0-9]+), guid: ([0-9a-fA-F]{32}),\s*type: 3\})");
    for (std::sregex_iterator it(curves.begin(), curves.end(), key), end; it != end; ++it) {
        if (frames.size() >= request.limits.maximumAssets) throw std::runtime_error("sprite key budget exceeded");
        const auto guid = unity_detail::foldAscii((*it)[3].str()), id = (*it)[2].str();
        if (!sources.contains(guid)) throw std::runtime_error("missing sprite GUID " + guid);
        const auto identity = guid + "/" + id;
        if (!cache.contains(identity)) cache.emplace(identity, sprite(request, sources.at(guid), id));
        auto         frame = cache.at(identity);
        const double raw   = scalar("t:" + (*it)[1].str(), R"(t:(.*))");
        const double time  = std::round(raw * fps) / fps;
        if (std::abs(time - raw) > 1e-5) throw std::runtime_error("sprite keys must align to the authored sample rate");
        frame["time"] = time;
        images.insert(frame.at("image").asString());
        frames.emplace_back(std::move(frame));
    }
    const std::regex times(R"(\n    - time:)");
    if (frames.size() !=
        std::size_t(std::distance(std::sregex_iterator(curves.begin(), curves.end(), times), std::sregex_iterator{})))
        throw std::runtime_error("unresolved or malformed sprite key");
    Value definition(Value::Object{{"schema", "eve.sprite-animation"},
                                   {"schemaVersion", 1},
                                   {"duration", duration},
                                   {"loop", loop == 1},
                                   {"sampleRate", fps},
                                   {"frames", std::move(frames)}});
    auto  validated = asset::SpriteAnimationClip::decode(definition);
    if (!validated) return Result<PreparedAssetImport>::failure(validated.status());
    auto manifest = detail::baseManifest(request.package, "eve.unity-sprite-animation/1");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    PreparedAssetImport out;
    out.manifest   = std::move(manifest).takeValue();
    const auto id  = request.package.packageId.child("unity:" + source.guid + ":7400000");
    auto       ref = detail::assetRef(id);
    if (!ref) return Result<PreparedAssetImport>::failure(ref.status());
    auto json = definition.toJson();
    if (!json) return Result<PreparedAssetImport>::failure(json.status());
    std::vector<std::uint8_t> bytes(json.value().begin(), json.value().end());
    const auto                path = "assets/" + id.format() + "/asset.json";
    out.manifest.assets.push_back({ref.value(),
                                   "eve.sprite-animation",
                                   SchemaVersion(1),
                                   path,
                                   detail::sha256(bytes),
                                   {"animation", "sprite", "source:unity"}});
    out.entries.push_back({path, std::move(bytes)});
    out.manifest.entrypoints.emplace("default", ref.value());
    out.sourceMappings.push_back({"7400000", ref.value()});
    for (const auto& image : images) {
        auto dependency = AssetRef::parse(image);
        if (!dependency) return Result<PreparedAssetImport>::failure(dependency.status());
        out.manifest.dependencies.push_back({ref.value(),
                                             dependency.value(),
                                             asset::EvaDependencyKind::RuntimeRequired,
                                             "spriteFrame",
                                             {},
                                             "eve.image/2",
                                             {}});
    }
    out.findings.push_back({source.path, "AnimationClip.sprite", ImportDisposition::Translated,
                            "explicit sprite keys, rectangles, pivots, pixel scale, filter and looping preserved"});
    return Result<PreparedAssetImport>::success(std::move(out));
} catch (const std::exception& error) {
    return detail::failure<PreparedAssetImport>(DiagnosticCode::ParseError, error.what(), source.path);
}
}  // namespace eve::asset_import
