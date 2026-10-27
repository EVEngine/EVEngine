#include <bit>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/BakedAssetFile.h"
#include "physics/backend/bake/DeformTrackInternal.h"
namespace eve::physics {
namespace {
constexpr std::uint64_t MaxBytes = 36 + 65536 + 4 * 6291456 + 8 * 1048576 + 16 * 2000000 + 32;
Diagnostic              malformed(const char* message) {
    return Diagnostic::error(DiagnosticCode::SerializationError, message, "deformTrack");
}
void word(std::string& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<char>((value >> shift) & 255));
}
struct Reader {
    std::string_view bytes;
    std::size_t      cursor = 0;
    std::string_view take(std::size_t count) {
        if (count > bytes.size() - cursor) throw std::runtime_error("truncated deformation asset");
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
Result<void> DeformTrack::save(std::string_view path) const {
    auto recipeBytes = data_->recipe->encode();
    if (!recipeBytes) return Result<void>::failure(recipeBytes.status());
    const auto  topology = this->topology();
    std::string bytes    = "EVEDEFM1";
    word(bytes, 1);
    word(bytes, topology.primitive() == DeformPrimitive::Segments ? 2u : 3u);
    word(bytes, static_cast<std::uint32_t>(topology.vertexCount()));
    word(bytes, static_cast<std::uint32_t>(topology.indices().size()));
    word(bytes, static_cast<std::uint32_t>(topology.uv().size()));
    word(bytes, static_cast<std::uint32_t>(frameCount()));
    word(bytes, static_cast<std::uint32_t>(recipeBytes.value().size()));
    bytes += recipeBytes.value();
    for (auto index : topology.indices()) word(bytes, index);
    for (auto uv : topology.uv()) {
        word(bytes, std::bit_cast<std::uint32_t>(uv.u));
        word(bytes, std::bit_cast<std::uint32_t>(uv.v));
    }
    for (const auto& frame : data_->frames) {
        word(bytes, std::bit_cast<std::uint32_t>(frame.seconds));
        for (const auto point : frame.positions)
            for (float value : {point.x, point.y, point.z}) word(bytes, std::bit_cast<std::uint32_t>(value));
    }
    bytes += bakedAssetContentKey(bytes);
    return publishBakedAsset(path, bytes);
}
Result<DeformTrack> DeformTrack::load(std::string_view path) {
    if (path.empty() || path.find('\0') != std::string_view::npos)
        return Result<DeformTrack>::failure(malformed("invalid deformation asset path"));
    std::ifstream file(std::string(path), std::ios::binary | std::ios::ate);
    if (!file) {
        std::error_code error;
        const auto      status = std::filesystem::status(std::filesystem::path(path), error);
        const bool      missing =
            error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status));
        return Result<DeformTrack>::failure(
            Diagnostic::error(missing ? DiagnosticCode::NotFound : DiagnosticCode::Failed,
                              "deformation asset cannot be opened", std::string(path)));
    }
    const auto size = file.tellg();
    if (size < 68 || static_cast<std::uint64_t>(size) > MaxBytes)
        return Result<DeformTrack>::failure(malformed("deformation asset exceeds size bounds"));
    std::string bytes(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) return Result<DeformTrack>::failure(malformed("deformation asset read failed"));
    const auto payload = std::string_view(bytes).substr(0, bytes.size() - 32);
    if (bakedAssetContentKey(payload) != std::string_view(bytes).substr(bytes.size() - 32))
        return Result<DeformTrack>::failure(
            Diagnostic::error(DiagnosticCode::HashMismatch, "deformation asset checksum mismatch", "deformTrack"));
    try {
        Reader reader{payload};
        if (reader.take(8) != "EVEDEFM1" || reader.word() != 1)
            return Result<DeformTrack>::failure(malformed("unknown deformation schema/version"));
        const auto arity = reader.word(), vertices = reader.word(), indices = reader.word(), uvCount = reader.word();
        const auto frames = reader.word(), recipeSize = reader.word();
        if ((arity != 2 && arity != 3) || !vertices || vertices > 1048576 || !indices || indices > 6291456 ||
            indices % arity || (uvCount != 0 && uvCount != vertices) || !frames ||
            std::uint64_t(frames) * vertices > 2000000 || !recipeSize || recipeSize > 65536)
            return Result<DeformTrack>::failure(malformed("invalid deformation asset counts"));
        const auto expected =
            36ull + recipeSize + 4ull * indices + 8ull * uvCount + std::uint64_t(frames) * (4 + 12ull * vertices);
        if (expected != payload.size())
            return Result<DeformTrack>::failure(malformed("truncated or trailing deformation data"));
        auto recipe = BakeRecipe::decode(reader.take(recipeSize));
        if (!recipe) return Result<DeformTrack>::failure(recipe.status());
        std::vector<std::uint32_t> connectivity;
        connectivity.reserve(indices);
        for (std::uint32_t i = 0; i < indices; ++i) connectivity.push_back(reader.word());
        std::vector<DeformUV> uv;
        uv.reserve(uvCount);
        for (std::uint32_t i = 0; i < uvCount; ++i) uv.push_back({reader.real(), reader.real()});
        auto topology = DeformTopology::prepare(arity == 2 ? DeformPrimitive::Segments : DeformPrimitive::Triangles,
                                                vertices, std::move(connectivity), std::move(uv));
        if (!topology) return Result<DeformTrack>::failure(topology.status());
        std::vector<DeformFrame> candidate;
        candidate.reserve(frames);
        for (std::uint32_t f = 0; f < frames; ++f) {
            DeformFrame frame{reader.real(), topology.value(), {}};
            frame.positions.reserve(vertices);
            for (std::uint32_t v = 0; v < vertices; ++v)
                frame.positions.push_back({reader.real(), reader.real(), reader.real()});
            candidate.push_back(std::move(frame));
        }
        return prepare(std::move(candidate), recipe.value());
    } catch (const std::exception&) {
        return Result<DeformTrack>::failure(malformed("deformation asset decoding failed"));
    }
}
Result<DeformTrack> DeformTrack::loadForRecipe(std::string_view path, const BakeRecipe& expected) {
    auto expectedBytes = expected.encode();
    if (!expectedBytes) return Result<DeformTrack>::failure(expectedBytes.status());
    auto loaded = load(path);
    if (!loaded) return loaded;
    auto actual = loaded.value().recipe()->encode();
    if (!actual) return Result<DeformTrack>::failure(actual.status());
    if (actual.value() != expectedBytes.value())
        return Result<DeformTrack>::failure(
            Diagnostic::error(DiagnosticCode::HashMismatch, "deformation asset provenance mismatch", "deformTrack"));
    return loaded;
}
}  // namespace eve::physics
