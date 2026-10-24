#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/BakedAssetFile.h"
#include "physics/bake/RigidPoseTrackInternal.h"

namespace eve::physics {
namespace {
constexpr std::uint64_t MaxSamples = 2000000, MaxTracks = 16384;
constexpr std::uint64_t MaxBytes = 24 + 16 * MaxTracks + 32 * MaxSamples + 32 + 4 + 65536;
Diagnostic              malformed(const char* message) {
    return Diagnostic::error(DiagnosticCode::SerializationError, message, "rigidPoseTrack");
}
void word(std::ostream& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out.put(static_cast<char>((value >> (8 * i)) & 255));
}
std::uint32_t word(std::istream& in) {
    std::uint32_t result = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const auto value = in.get();
        if (value < 0) throw std::runtime_error("truncated rigid track");
        result |= static_cast<std::uint32_t>(value) << (8 * i);
    }
    return result;
}
void  real(std::ostream& out, float value) { word(out, std::bit_cast<std::uint32_t>(value)); }
float real(std::istream& in) { return std::bit_cast<float>(word(in)); }
}  // namespace
Result<void> RigidPoseTrack::save(std::string_view path) const {
    std::ostringstream out(std::ios::binary);
    out.write("EVERIGD1", 8);
    word(out, data_->recipe ? 2u : 1u);
    word(out, space() == RigidTrackSpace::PlanarPixels ? 1u : 2u);
    word(out, static_cast<std::uint32_t>(trackCount()));
    word(out, static_cast<std::uint32_t>(frameCount()));
    if (data_->recipe) {
        auto encoded = data_->recipe->encode();
        if (!encoded) return Result<void>::failure(encoded.status());
        word(out, static_cast<std::uint32_t>(encoded.value().size()));
        out.write(encoded.value().data(), static_cast<std::streamsize>(encoded.value().size()));
    }
    for (const auto& sample : data_->frames.front().samples) {
        const auto bytes = sample.id.bytes();
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    for (const auto& frame : data_->frames) {
        real(out, frame.seconds);
        for (const auto& sample : frame.samples) {
            const auto& p = sample.pose;
            for (float value : {p.x, p.y, p.z, p.qx, p.qy, p.qz, p.qw}) real(out, value);
        }
    }
    if (!out) return Result<void>::failure(malformed("rigid track serialization failed"));
    auto bytes = out.str();
    bytes += bakedAssetContentKey(bytes);
    return publishBakedAsset(path, bytes);
}
Result<RigidPoseTrack> RigidPoseTrack::load(std::string_view path) {
    if (path.empty() || path.find('\0') != std::string_view::npos)
        return Result<RigidPoseTrack>::failure(malformed("invalid rigid track path"));
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file) {
        std::error_code error;
        const auto      status = std::filesystem::status(std::filesystem::path(path), error);
        const bool      missing =
            error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status));
        return Result<RigidPoseTrack>::failure(
            Diagnostic::error(missing ? DiagnosticCode::NotFound : DiagnosticCode::Failed,
                              "rigid track cannot be opened", std::string(path)));
    }
    const auto size = file.tellg();
    if (size < 56 || static_cast<std::uint64_t>(size) > MaxBytes)
        return Result<RigidPoseTrack>::failure(malformed("rigid track file size outside budget"));
    std::string bytes(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) return Result<RigidPoseTrack>::failure(malformed("rigid track read failed"));
    if (bakedAssetContentKey(std::string_view(bytes).substr(0, bytes.size() - 32)) !=
        std::string_view(bytes).substr(bytes.size() - 32))
        return Result<RigidPoseTrack>::failure(
            Diagnostic::error(DiagnosticCode::HashMismatch, "rigid track checksum mismatch", "rigidPoseTrack"));
    bytes.resize(bytes.size() - 32);
    const auto         payloadSize = bytes.size();
    std::istringstream in(std::move(bytes), std::ios::binary);
    try {
        char magic[8];
        in.read(magic, 8);
        const auto version = word(in);
        if (!in || std::string_view(magic, 8) != "EVERIGD1" || (version != 1 && version != 2))
            return Result<RigidPoseTrack>::failure(malformed("unknown rigid track schema/version"));
        const auto spaceCode = word(in), tracks = word(in), frames = word(in);
        if ((spaceCode != 1 && spaceCode != 2) || !tracks || tracks > MaxTracks || !frames ||
            static_cast<std::uint64_t>(frames) * tracks > MaxSamples)
            return Result<RigidPoseTrack>::failure(malformed("invalid rigid track counts or coordinates"));
        std::shared_ptr<const BakeRecipe> provenance;
        std::uint64_t                     metadataBytes = 0;
        if (version == 2) {
            const auto recipeSize = word(in);
            if (recipeSize == 0 || recipeSize > 65536 || recipeSize > payloadSize - 28)
                return Result<RigidPoseTrack>::failure(malformed("invalid rigid recipe length"));
            std::string encoded(recipeSize, '\0');
            in.read(encoded.data(), static_cast<std::streamsize>(recipeSize));
            if (!in) return Result<RigidPoseTrack>::failure(malformed("truncated rigid recipe"));
            auto decoded = BakeRecipe::decode(encoded);
            if (!decoded) return Result<RigidPoseTrack>::failure(decoded.status());
            provenance    = std::make_shared<const BakeRecipe>(std::move(decoded).takeValue());
            metadataBytes = 4ull + recipeSize;
        }
        const auto expected =
            24 + metadataBytes + 16ull * tracks + static_cast<std::uint64_t>(frames) * (4 + 28ull * tracks);
        if (payloadSize != expected)
            return Result<RigidPoseTrack>::failure(malformed("truncated or trailing rigid track fields"));
        std::vector<PersistentId> ids;
        ids.reserve(tracks);
        for (std::uint32_t i = 0; i < tracks; ++i) {
            PersistentId::Bytes identity{};
            in.read(reinterpret_cast<char*>(identity.data()), static_cast<std::streamsize>(identity.size()));
            const PersistentId id(identity);
            if (id.isNil() || (!ids.empty() && id <= ids.back()))
                return Result<RigidPoseTrack>::failure(malformed("noncanonical rigid track identities"));
            ids.push_back(id);
        }
        std::vector<RigidTrackFrame> candidate;
        candidate.reserve(frames);
        for (std::uint32_t f = 0; f < frames; ++f) {
            RigidTrackFrame frame{real(in), {}};
            frame.samples.reserve(tracks);
            for (auto id : ids) {
                RigidTrackPose p;
                p.x  = real(in);
                p.y  = real(in);
                p.z  = real(in);
                p.qx = real(in);
                p.qy = real(in);
                p.qz = real(in);
                p.qw = real(in);
                frame.samples.push_back({id, p});
            }
            candidate.push_back(std::move(frame));
        }
        const auto space = spaceCode == 1 ? RigidTrackSpace::PlanarPixels : RigidTrackSpace::Metres3D;
        if (provenance) return prepare(space, std::move(candidate), *provenance);
        return prepare(space, std::move(candidate));
    } catch (const std::exception&) {
        return Result<RigidPoseTrack>::failure(malformed("rigid track decoding failed"));
    }
}
Result<RigidPoseTrack> RigidPoseTrack::loadForRecipe(std::string_view path, const BakeRecipe& expected) {
    auto expectedBytes = expected.encode();
    if (!expectedBytes) return Result<RigidPoseTrack>::failure(expectedBytes.status());
    auto loaded = load(path);
    if (!loaded) return loaded;
    const auto provenance = loaded.value().recipe();
    if (!provenance)
        return Result<RigidPoseTrack>::failure(
            Diagnostic::error(DiagnosticCode::PreconditionViolation,
                              "rigid asset has no provenance; explicit playback or rebake required", "rigidPoseTrack"));
    auto actual = provenance->encode();
    if (!actual) return Result<RigidPoseTrack>::failure(actual.status());
    if (actual.value() != expectedBytes.value())
        return Result<RigidPoseTrack>::failure(
            Diagnostic::error(DiagnosticCode::HashMismatch, "rigid asset provenance mismatch", "rigidPoseTrack"));
    return loaded;
}
}  // namespace eve::physics
