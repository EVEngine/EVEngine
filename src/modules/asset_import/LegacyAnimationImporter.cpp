#include "asset_import/LegacyAnimationImporter.h"

#include "asset_import/ImportCommon.h"

#include <bit>
#include <cmath>
#include <sstream>
#include <set>

namespace eve::asset_import {
namespace {

struct Bone {
    std::int32_t parent = -1;
    std::string name;
    std::array<float, 3> position{};
    std::array<float, 4> rotation{0, 0, 0, 1};
    std::array<float, 3> scale{1, 1, 1};
    bool hasBind = false;
};
struct VecKey { float time = 0; std::array<float, 3> value{}; };
struct RotKey { float time = 0; std::array<float, 4> value{}; };
struct Track {
    std::uint32_t bone = 0;
    std::uint32_t expectedPosition = 0, expectedRotation = 0, expectedScale = 0;
    std::vector<VecKey> position, scale;
    std::vector<RotKey> rotation;
};
struct Marker { float time = 0; std::string name; };
struct Fixture {
    std::vector<Bone> bones;
    std::string clipName;
    float duration = 0, rate = 0;
    bool loop = true;
    std::vector<Track> tracks;
    std::vector<Marker> markers;
};

void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) out.push_back(static_cast<std::uint8_t>(value >> shift));
}
void putFloat(std::vector<std::uint8_t>& out, float value) { put32(out, std::bit_cast<std::uint32_t>(value)); }
void putString(std::vector<std::uint8_t>& out, std::string_view value) {
    put32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}
std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r");
    return value.substr(first, last - first + 1);
}
bool finite(float value) { return std::isfinite(value); }
bool complete(std::istringstream& line) { line >> std::ws; return line.eof(); }

Result<Fixture> parseFixture(const LegacyAnimationImportRequest& request) {
    if (request.text.size() > request.limits.maximumSourceBytes)
        return detail::failure<Fixture>(DiagnosticCode::InvalidArgument, "animation fixture exceeds source budget");
    Fixture result;
    std::istringstream input(request.text);
    std::string text;
    bool magic = false, ended = false;
    std::uint32_t declaredBones = 0;
    Track* track = nullptr;
    std::size_t lineNumber = 0;
    while (std::getline(input, text)) {
        ++lineNumber;
        text = trim(std::move(text));
        if (text.empty() || text.front() == '#') continue;
        std::istringstream line(text);
        std::string tag;
        line >> tag;
        const auto path = request.sourceName + ":" + std::to_string(lineNumber);
        if (!magic) {
            int version = 0;
            if (tag != "EVA" || !(line >> version) || version != 1 || !complete(line))
                return detail::failure<Fixture>(DiagnosticCode::UnknownVersion,
                                                "legacy animation must begin with EVA 1", path);
            magic = true;
        } else if (tag == "skeleton") {
            if (!(line >> declaredBones) || declaredBones == 0 ||
                declaredBones > request.limits.maximumAssets || !complete(line))
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid skeleton count", path);
            result.bones.reserve(declaredBones);
        } else if (tag == "bone") {
            std::uint32_t index = 0;
            Bone bone;
            if (!(line >> index >> bone.parent) || index != result.bones.size())
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid bone index", path);
            std::getline(line, bone.name); bone.name = trim(std::move(bone.name));
            if (bone.name.empty() || bone.name.size() > request.limits.maximumStringBytes ||
                bone.parent >= static_cast<std::int32_t>(index) || bone.parent < -1)
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid bone hierarchy", path);
            result.bones.push_back(std::move(bone));
        } else if (tag == "bind") {
            std::uint32_t index = 0;
            if (!(line >> index) || index >= result.bones.size())
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid bind index", path);
            auto& bone = result.bones[index];
            if (!(line >> bone.position[0] >> bone.position[1] >> bone.position[2] >>
                  bone.rotation[0] >> bone.rotation[1] >> bone.rotation[2] >> bone.rotation[3] >>
                  bone.scale[0] >> bone.scale[1] >> bone.scale[2]) || !complete(line))
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid bind transform", path);
            bone.hasBind = true;
        } else if (tag == "clip") {
            std::getline(line, result.clipName); result.clipName = trim(std::move(result.clipName));
            if (result.clipName.empty() || result.clipName.size() > request.limits.maximumStringBytes)
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid clip name", path);
        } else if (tag == "duration") {
            if (!(line >> result.duration) || !complete(line) || !finite(result.duration) || result.duration < 0)
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid clip duration", path);
        } else if (tag == "rate") {
            if (!(line >> result.rate) || !complete(line) || !finite(result.rate) || result.rate <= 0)
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid sample rate", path);
        } else if (tag == "loop") {
            int value = 0;
            if (!(line >> value) || (value != 0 && value != 1) || !complete(line))
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid loop flag", path);
            result.loop = value != 0;
        } else if (tag == "sync") {
            Marker marker;
            if (!(line >> marker.time) || !finite(marker.time))
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid sync marker", path);
            std::getline(line, marker.name); marker.name = trim(std::move(marker.name));
            if (marker.name.empty()) return detail::failure<Fixture>(DiagnosticCode::ParseError,
                                                                     "empty sync marker", path);
            result.markers.push_back(std::move(marker));
            if (result.markers.size() > request.limits.maximumAssets)
                return detail::failure<Fixture>(DiagnosticCode::InvalidArgument, "too many sync markers", path);
        } else if (tag == "track") {
            Track value;
            if (!(line >> value.bone >> value.expectedPosition >> value.expectedRotation >> value.expectedScale) ||
                !complete(line) || value.bone >= result.bones.size())
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid track header", path);
            result.tracks.push_back(std::move(value)); track = &result.tracks.back();
            if (result.tracks.size() > request.limits.maximumAssets)
                return detail::failure<Fixture>(DiagnosticCode::InvalidArgument, "too many animation tracks", path);
        } else if (tag == "p" || tag == "s") {
            VecKey key;
            if (!track || !(line >> key.time >> key.value[0] >> key.value[1] >> key.value[2]) ||
                !complete(line) || !finite(key.time) || !finite(key.value[0]) || !finite(key.value[1]) || !finite(key.value[2]))
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid vector key", path);
            (tag == "p" ? track->position : track->scale).push_back(key);
        } else if (tag == "r") {
            RotKey key;
            if (!track || !(line >> key.time >> key.value[0] >> key.value[1] >> key.value[2] >> key.value[3]) ||
                !complete(line) || !finite(key.time) || !finite(key.value[0]) || !finite(key.value[1]) ||
                !finite(key.value[2]) || !finite(key.value[3]))
                return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid rotation key", path);
            track->rotation.push_back(key);
        } else if (tag == "end") {
            if (!complete(line)) return detail::failure<Fixture>(DiagnosticCode::ParseError, "invalid end", path);
            ended = true; break;
        } else {
            return detail::failure<Fixture>(DiagnosticCode::ParseError, "unknown animation fixture record", path);
        }
    }
    if (!magic || !ended || result.bones.size() != declaredBones || result.clipName.empty() || result.rate <= 0)
        return detail::failure<Fixture>(DiagnosticCode::ParseError, "animation fixture is incomplete");
    for (const auto& bone : result.bones)
        if (!bone.hasBind) return detail::failure<Fixture>(DiagnosticCode::ParseError, "bone bind is missing", bone.name);
    for (const auto& value : result.tracks)
        if (value.position.size() != value.expectedPosition || value.rotation.size() != value.expectedRotation ||
            value.scale.size() != value.expectedScale)
            return detail::failure<Fixture>(DiagnosticCode::ParseError, "track key count does not match declaration");
    std::set<std::uint32_t> trackBones;
    std::uint64_t decodedBytes = 0;
    const auto validTimes = [&](const auto& keys) {
        float previous = -1.0f;
        for (const auto& key : keys) {
            if (key.time < 0 || key.time > result.duration || key.time < previous) return false;
            previous = key.time;
        }
        return true;
    };
    for (const auto& value : result.tracks) {
        if (!trackBones.emplace(value.bone).second || !validTimes(value.position) ||
            !validTimes(value.rotation) || !validTimes(value.scale))
            return detail::failure<Fixture>(DiagnosticCode::Conflict,
                                            "tracks must be unique and key times canonical");
        const std::uint64_t keys = value.position.size() + value.rotation.size() + value.scale.size();
        if (keys > request.limits.maximumDecodedBytes / 24 ||
            decodedBytes > request.limits.maximumDecodedBytes - keys * 24)
            return detail::failure<Fixture>(DiagnosticCode::InvalidArgument,
                                            "animation key data exceeds decoded budget");
        decodedBytes += keys * 24;
    }
    return Result<Fixture>::success(std::move(result));
}

}  // namespace

Result<PreparedAssetImport> prepareLegacyAnimationImport(const LegacyAnimationImportRequest& request) {
    auto parsed = parseFixture(request);
    if (!parsed) return Result<PreparedAssetImport>::failure(parsed.status());
    auto fixture = std::move(parsed).takeValue();
    auto manifest = detail::baseManifest(request.package, "eve.legacy-animation-text/1");
    if (!manifest) return Result<PreparedAssetImport>::failure(manifest.status());
    PreparedAssetImport output; output.manifest = std::move(manifest).takeValue();
    const auto skeletonId = request.package.packageId.child("animation:skeleton");
    const auto clipId = request.package.packageId.child("animation:clip:" + fixture.clipName);
    auto skeletonRef = detail::assetRef(skeletonId); auto clipRef = detail::assetRef(clipId);
    if (!skeletonRef) return Result<PreparedAssetImport>::failure(skeletonRef.status());
    if (!clipRef) return Result<PreparedAssetImport>::failure(clipRef.status());

    std::vector<std::uint8_t> skeletonBlob{'E','V','S','K','E','L',0,1};
    put32(skeletonBlob, static_cast<std::uint32_t>(fixture.bones.size()));
    for (auto bone : fixture.bones) {
        put32(skeletonBlob, static_cast<std::uint32_t>(bone.parent)); putString(skeletonBlob, bone.name);
        bone.position = {bone.position[0] * .01f, bone.position[1] * .01f, -bone.position[2] * .01f};
        bone.rotation = {-bone.rotation[0], -bone.rotation[1], bone.rotation[2], bone.rotation[3]};
        for (float value : bone.position) putFloat(skeletonBlob, value);
        for (float value : bone.rotation) putFloat(skeletonBlob, value);
        for (float value : bone.scale) putFloat(skeletonBlob, value);
    }
    std::vector<std::uint8_t> clipBlob{'E','V','A','N','I','M',0,1};
    putFloat(clipBlob, fixture.duration); putFloat(clipBlob, fixture.rate); put32(clipBlob, fixture.loop ? 1 : 0);
    put32(clipBlob, static_cast<std::uint32_t>(fixture.markers.size()));
    put32(clipBlob, static_cast<std::uint32_t>(fixture.tracks.size()));
    for (const auto& marker : fixture.markers) { putFloat(clipBlob, marker.time); putString(clipBlob, marker.name); }
    for (auto track : fixture.tracks) {
        put32(clipBlob, track.bone);
        put32(clipBlob, static_cast<std::uint32_t>(track.position.size()));
        put32(clipBlob, static_cast<std::uint32_t>(track.rotation.size()));
        put32(clipBlob, static_cast<std::uint32_t>(track.scale.size()));
        for (auto key : track.position) { putFloat(clipBlob, key.time); putFloat(clipBlob, key.value[0] * .01f); putFloat(clipBlob, key.value[1] * .01f); putFloat(clipBlob, -key.value[2] * .01f); }
        for (auto key : track.rotation) { putFloat(clipBlob, key.time); putFloat(clipBlob, -key.value[0]); putFloat(clipBlob, -key.value[1]); putFloat(clipBlob, key.value[2]); putFloat(clipBlob, key.value[3]); }
        for (auto key : track.scale) { putFloat(clipBlob, key.time); for (float value : key.value) putFloat(clipBlob, value); }
    }
    const std::string skeletonRoot = "assets/" + skeletonId.format() + "/";
    const std::string clipRoot = "assets/" + clipId.format() + "/";
    Value::Object skeletonDefinition{{"schema", Value("eve.skeleton")}, {"schemaVersion", Value(std::int64_t(1))},
        {"boneCount", Value(static_cast<std::int64_t>(fixture.bones.size()))}, {"coordinateSystem", Value("right-handed-x-right-y-up-minus-z-forward")},
        {"lengthUnit", Value("meter")}, {"blob", Value(skeletonRoot + "skeleton.bin")}};
    Value::Object clipDefinition{{"schema", Value("eve.animation-clip")}, {"schemaVersion", Value(std::int64_t(1))},
        {"name", Value(fixture.clipName)}, {"durationSeconds", Value(double(fixture.duration))},
        {"sampleRate", Value(double(fixture.rate))}, {"loop", Value(fixture.loop)},
        {"skeleton", Value(skeletonRef.value().format())}, {"blob", Value(clipRoot + "clip.bin")}};
    auto skeletonJson = Value(std::move(skeletonDefinition)).toJson(); auto clipJson = Value(std::move(clipDefinition)).toJson();
    if (!skeletonJson) return Result<PreparedAssetImport>::failure(skeletonJson.status());
    if (!clipJson) return Result<PreparedAssetImport>::failure(clipJson.status());
    output.manifest.assets.push_back({skeletonRef.value(), "eve.skeleton", SchemaVersion(1), skeletonRoot + "asset.json",
        detail::sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(skeletonJson.value().data()), skeletonJson.value().size())), {"animation", "skeleton"}});
    output.manifest.assets.push_back({clipRef.value(), "eve.animation-clip", SchemaVersion(1), clipRoot + "asset.json",
        detail::sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(clipJson.value().data()), clipJson.value().size())), {"animation", "clip"}});
    output.manifest.dependencies.push_back({clipRef.value(), skeletonRef.value(), asset::EvaDependencyKind::RuntimeRequired,
                                            "skeleton", {}, "eve.skeleton/1"});
    output.manifest.entrypoints.emplace("default", clipRef.value());
    output.manifest.provenance["sourceName"] = Value(request.sourceName);
    output.manifest.provenance["sourceHash"] = Value(detail::sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(request.text.data()), request.text.size())));
    output.manifest.provenance["sourceTransform"] = Value("centimeter,+Z-forward -> meter,-Z-forward");
    output.entries = {{skeletonRoot + "asset.json", {skeletonJson.value().begin(), skeletonJson.value().end()}},
                      {skeletonRoot + "skeleton.bin", std::move(skeletonBlob)},
                      {clipRoot + "asset.json", {clipJson.value().begin(), clipJson.value().end()}},
                      {clipRoot + "clip.bin", std::move(clipBlob)}};
    output.sourceMappings.push_back({request.sourceName + "#skeleton", skeletonRef.value()});
    output.sourceMappings.push_back({request.sourceName + "#clip:" + fixture.clipName, clipRef.value()});
    output.findings.push_back({request.sourceName, "EVA 1 animation fixture", ImportDisposition::Translated,
                               "legacy centimetre/+Z-forward data converted to canonical assets"});
    auto report = detail::finalizeImportReport(output, request.package, "legacy-animation-text", "EVA 1");
    if (!report) return Result<PreparedAssetImport>::failure(report.status());
    return Result<PreparedAssetImport>::success(std::move(output));
}

}  // namespace eve::asset_import
