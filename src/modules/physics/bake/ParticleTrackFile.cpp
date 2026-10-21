#include <bit>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/BakedAssetFile.h"
#include "physics/bake/ParticleTrackInternal.h"
namespace eve::physics {
namespace {
constexpr std::uint64_t MaxBytes = 24 + 65536 + 28 * 65536 + 56 * 2000000 + 32;
Diagnostic              malformed(const char* message) {
    return Diagnostic::error(DiagnosticCode::SerializationError, message, "particleTrack");
}
void word(std::string& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<char>((value >> shift) & 255));
}
struct Reader {
    std::string_view bytes;
    std::size_t      cursor = 0;
    std::string_view take(std::size_t count) {
        if (count > bytes.size() - cursor) throw std::runtime_error("truncated particle asset");
        auto value = bytes.substr(cursor, count);
        cursor += count;
        return value;
    }
    std::uint32_t word() {
        const auto    data  = take(4);
        std::uint32_t value = 0;
        for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(static_cast<unsigned char>(data[i])) << (i * 8);
        return value;
    }
    float real() { return std::bit_cast<float>(word()); }
};
}  // namespace
Result<void> ParticleTrack::save(std::string_view path) const {
    auto recipeBytes = data_->recipe->encode();
    if (!recipeBytes) return Result<void>::failure(recipeBytes.status());
    std::string bytes = "EVEPART1";
    word(bytes, 1);
    word(bytes, std::bit_cast<std::uint32_t>(duration()));
    word(bytes, static_cast<std::uint32_t>(capacity()));
    word(bytes, static_cast<std::uint32_t>(recipeBytes.value().size()));
    bytes += recipeBytes.value();
    for (const auto& track : data_->trajectories) {
        for (auto byte : track.emitter.bytes()) bytes.push_back(static_cast<char>(byte));
        word(bytes, static_cast<std::uint32_t>(track.birthSerial));
        word(bytes, static_cast<std::uint32_t>(track.birthSerial >> 32));
        word(bytes, static_cast<std::uint32_t>(track.keys.size()));
        for (const auto& key : track.keys) {
            const auto& p = key.pose;
            for (float value :
                 {key.seconds, p.x, p.y, p.w, p.h, p.rotation, p.r, p.g, p.b, p.a, p.u0, p.v0, p.u1, p.v1})
                word(bytes, std::bit_cast<std::uint32_t>(value));
        }
    }
    bytes += bakedAssetContentKey(bytes);
    return publishBakedAsset(path, bytes);
}
Result<ParticleTrack> ParticleTrack::load(std::string_view path) {
    if (path.empty() || path.find('\0') != std::string_view::npos)
        return Result<ParticleTrack>::failure(malformed("invalid particle asset path"));
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file) {
        std::error_code error;
        const auto      status = std::filesystem::status(std::filesystem::path(path), error);
        const bool      missing =
            error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status));
        return Result<ParticleTrack>::failure(
            Diagnostic::error(missing ? DiagnosticCode::NotFound : DiagnosticCode::Failed,
                              "particle asset cannot be opened", std::string(path)));
    }
    const auto size = file.tellg();
    if (size < 56 || static_cast<std::uint64_t>(size) > MaxBytes)
        return Result<ParticleTrack>::failure(malformed("particle asset exceeds size bounds"));
    std::string bytes(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) return Result<ParticleTrack>::failure(malformed("particle asset read failed"));
    const auto payload = std::string_view(bytes).substr(0, bytes.size() - 32);
    if (bakedAssetContentKey(payload) != std::string_view(bytes).substr(bytes.size() - 32))
        return Result<ParticleTrack>::failure(
            Diagnostic::error(DiagnosticCode::HashMismatch, "particle asset checksum mismatch", "particleTrack"));
    try {
        Reader reader{payload};
        if (reader.take(8) != "EVEPART1" || reader.word() != 1)
            return Result<ParticleTrack>::failure(malformed("unknown particle schema/version"));
        const float duration = reader.real();
        const auto  count = reader.word(), recipeSize = reader.word();
        if (count > 65536 || !recipeSize || recipeSize > 65536)
            return Result<ParticleTrack>::failure(malformed("invalid particle asset counts"));
        auto recipe = BakeRecipe::decode(reader.take(recipeSize));
        if (!recipe) return Result<ParticleTrack>::failure(recipe.status());
        const auto    recordsStart = reader.cursor;
        std::uint64_t totalKeys    = 0;
        for (std::uint32_t i = 0; i < count; ++i) {
            reader.take(24);
            const auto keys = reader.word();
            totalKeys += keys;
            if (keys < 2 || totalKeys > 2000000)
                return Result<ParticleTrack>::failure(malformed("invalid particle key budget"));
            reader.take(std::size_t(keys) * 56);
        }
        if (reader.cursor != payload.size()) return Result<ParticleTrack>::failure(malformed("trailing particle data"));
        reader.cursor = recordsStart;
        std::vector<ParticleTrajectory> candidate;
        candidate.reserve(count);
        for (std::uint32_t i = 0; i < count; ++i) {
            ParticleTrajectory  track;
            PersistentId::Bytes identity{};
            const auto          encodedId = reader.take(16);
            for (std::size_t j = 0; j < identity.size(); ++j) identity[j] = static_cast<unsigned char>(encodedId[j]);
            track.emitter     = PersistentId(identity);
            const auto low    = reader.word();
            track.birthSerial = std::uint64_t(low) | (std::uint64_t(reader.word()) << 32);
            const auto keys   = reader.word();
            track.keys.reserve(keys);
            for (std::uint32_t k = 0; k < keys; ++k) {
                ParticleTrackKey key;
                key.seconds = reader.real();
                auto& p     = key.pose;
                p           = {reader.real(), reader.real(), reader.real(), reader.real(), reader.real(),
                               reader.real(), reader.real(), reader.real(), reader.real(), reader.real(),
                               reader.real(), reader.real(), reader.real()};
                track.keys.push_back(key);
            }
            candidate.push_back(std::move(track));
        }
        return prepare(duration, std::move(candidate), recipe.value());
    } catch (const std::exception&) {
        return Result<ParticleTrack>::failure(malformed("particle asset decoding failed"));
    }
}
Result<ParticleTrack> ParticleTrack::loadForRecipe(std::string_view path, const BakeRecipe& expected) {
    auto expectedBytes = expected.encode();
    if (!expectedBytes) return Result<ParticleTrack>::failure(expectedBytes.status());
    auto loaded = load(path);
    if (!loaded) return loaded;
    auto actual = loaded.value().recipe()->encode();
    if (!actual) return Result<ParticleTrack>::failure(actual.status());
    if (actual.value() != expectedBytes.value())
        return Result<ParticleTrack>::failure(
            Diagnostic::error(DiagnosticCode::HashMismatch, "particle asset provenance mismatch", "particleTrack"));
    return loaded;
}
}  // namespace eve::physics
