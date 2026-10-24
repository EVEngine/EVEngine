#include "physics/bake/collapse/CollapseAnimation.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#if defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#endif
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/BakedAssetFile.h"
#include "physics/bake/collapse/CollapseAnimationInternal.h"
#include "physics/bake/collapse/FractureGeometry.h"

namespace eve::physics {
using namespace collapse_detail;
std::string CollapseAnimation::contentKey(std::string_view source) { return bakedAssetContentKey(source); }

namespace {
constexpr std::streamoff MaxFileBytes = static_cast<std::streamoff>(MaxSamples) * 56 + 16000056 + MaxRecipeBytes + 4;
void                     write32(std::ostream& out, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) out.put(static_cast<char>((v >> (i * 8)) & 255));
}
std::uint32_t read32(std::istream& in) {
    std::uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) {
        const int c = in.get();
        if (c < 0) {
            in.setstate(std::ios::failbit);
            return 0;
        }
        v |= static_cast<std::uint32_t>(c) << (i * 8);
    }
    return v;
}
void  writeFloat(std::ostream& out, float v) { write32(out, std::bit_cast<std::uint32_t>(v)); }
float readFloat(std::istream& in) { return std::bit_cast<float>(read32(in)); }
}  // namespace
Result<void> CollapseAnimation::beginBake(const CollapseBakeRecipe& recipe) {
    if (!frames_.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "provenance must be set before capturing any frame",
                                                       "collapseAnimation"));
    if (recipe.coordinates() != BakeCoordinates::MetresRightHandedYUp)
        return Result<void>::failure(invalid("collapse requires metre-space provenance"));
    auto encoded = recipe.encode();
    if (!encoded) return Result<void>::failure(encoded.status());
    recipe_ = std::make_shared<const CollapseBakeRecipe>(recipe);
    return Result<void>::success();
}
std::shared_ptr<const CollapseBakeRecipe> CollapseAnimation::recipe() const noexcept { return recipe_; }
Result<CollapseAnimation> CollapseAnimation::loadForRecipe(std::string_view path, const CollapseBakeRecipe& expected) {
    auto expectedBytes = expected.encode();
    if (!expectedBytes) return Result<CollapseAnimation>::failure(expectedBytes.status());
    auto loaded = load(path);
    if (!loaded) return loaded;
    if (!loaded.value().recipe_)
        return Result<CollapseAnimation>::failure(Diagnostic::error(
            DiagnosticCode::PreconditionViolation,
            "legacy animation has no bake provenance; explicit migration or rebake required", "collapseAnimation"));
    auto actual = loaded.value().recipe_->encode();
    if (!actual) return Result<CollapseAnimation>::failure(actual.status());
    if (actual.value() != expectedBytes.value())
        return Result<CollapseAnimation>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "bake provenance does not match the expected recipe", "collapseAnimation"));
    return loaded;
}
std::shared_ptr<const FractureGeometry> CollapseAnimation::fragmentGeometry() const noexcept { return geometry_; }
float CollapseAnimation::duration() const noexcept { return frames_.empty() ? 0 : frames_.back().seconds; }
int   CollapseAnimation::boneCount() const noexcept {
    return frames_.empty() ? 0 : static_cast<int>(frames_.front().bones.size());
}
int                      CollapseAnimation::frameCount() const noexcept { return static_cast<int>(frames_.size()); }
Result<BonePresentation> CollapseAnimation::sample(float seconds, int bone) const {
    if (!std::isfinite(seconds) || bone < 0 || bone >= boneCount())
        return Result<BonePresentation>::failure(invalid("invalid sample time or fragment"));
    auto         upper = std::upper_bound(frames_.begin(), frames_.end(), seconds,
                                          [](float t, const Frame& f) { return t < f.seconds; });
    const size_t index = static_cast<size_t>(bone);
    if (upper == frames_.begin()) return Result<BonePresentation>::success(upper->bones[index]);
    const auto& a = *(upper - 1);
    if (upper == frames_.end()) return Result<BonePresentation>::success(a.bones[index]);
    auto        result = a.bones[index];
    const auto& b      = upper->bones[index];
    const float t      = (seconds - a.seconds) / (upper->seconds - a.seconds);
    result.worldX      = std::lerp(result.worldX, b.worldX, t);
    result.worldY      = std::lerp(result.worldY, b.worldY, t);
    result.worldZ      = std::lerp(result.worldZ, b.worldZ, t);
    const float sign =
        (result.rotX * b.rotX + result.rotY * b.rotY + result.rotZ * b.rotZ + result.rotW * b.rotW) < 0 ? -1.f : 1.f;
    result.rotX      = std::lerp(result.rotX, sign * b.rotX, t);
    result.rotY      = std::lerp(result.rotY, sign * b.rotY, t);
    result.rotZ      = std::lerp(result.rotZ, sign * b.rotZ, t);
    result.rotW      = std::lerp(result.rotW, sign * b.rotW, t);
    const float norm = std::sqrt(result.rotX * result.rotX + result.rotY * result.rotY + result.rotZ * result.rotZ +
                                 result.rotW * result.rotW);
    result.rotX /= norm;
    result.rotY /= norm;
    result.rotZ /= norm;
    result.rotW /= norm;
    return Result<BonePresentation>::success(result);
}
Result<void> CollapseAnimation::save(std::string_view path) const {
    if (frames_.empty() || path.empty() || path.find('\0') != std::string_view::npos)
        return Result<void>::failure(invalid("save requires frames and a nonempty path without NUL"));
    std::string recipeBytes;
    if (recipe_) {
        auto encoded = recipe_->encode();
        if (!encoded) return Result<void>::failure(encoded.status());
        recipeBytes = std::move(encoded).takeValue();
    }
    std::ostringstream out(std::ios::binary);
    out.write("EVECOLL1", 8);
    write32(out, recipe_ ? 4U : 3U);
    write32(out, static_cast<std::uint32_t>(boneCount()));
    write32(out, static_cast<std::uint32_t>(frameCount()));
    write32(out, geometry_ ? static_cast<std::uint32_t>(geometry_->fragments.size()) : 0U);
    if (recipe_) {
        write32(out, static_cast<std::uint32_t>(recipeBytes.size()));
        out.write(recipeBytes.data(), static_cast<std::streamsize>(recipeBytes.size()));
    }
    if (geometry_)
        for (const auto& mesh : geometry_->fragments) {
            write32(out, static_cast<std::uint32_t>(mesh.positions.size() / 3));
            write32(out, static_cast<std::uint32_t>(mesh.indices.size() / 3));
            for (float v : mesh.positions) writeFloat(out, v);
            for (auto i : mesh.indices) write32(out, i);
            for (auto tag : mesh.materials) write32(out, tag);
        }
    for (const auto& f : frames_) {
        writeFloat(out, f.seconds);
        for (const auto& b : f.bones) {
            for (float v : {b.halfExtentX, b.halfExtentY, b.halfExtentZ, b.worldX, b.worldY, b.worldZ, b.rotX, b.rotY,
                            b.rotZ, b.rotW})
                writeFloat(out, v);
            write32(out, static_cast<std::uint32_t>(b.state));
            write32(out, static_cast<std::uint32_t>(b.clusterId));
            write32(out, static_cast<std::uint32_t>(b.fractureLevel));
        }
    }
    if (!out) return Result<void>::failure(invalid("animation encoding failed"));
    auto bytes = std::move(out).str();
    bytes += contentKey(bytes);
    if (bytes.size() > static_cast<std::size_t>(MaxFileBytes))
        return Result<void>::failure(invalid("encoded animation exceeds file budget"));
    return publishBakedAsset(path, bytes);
}

Result<CollapseAnimation> CollapseAnimation::load(std::string_view path) {
    if (path.empty() || path.find('\0') != std::string_view::npos)
        return Result<CollapseAnimation>::failure(invalid("load requires a path without NUL"));
    std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::error_code error;
        const auto      status = std::filesystem::status(std::filesystem::path(path), error);
        const bool      missing =
            error == std::errc::no_such_file_or_directory || (!error && !std::filesystem::exists(status));
        return Result<CollapseAnimation>::failure(Diagnostic::error(
            missing ? DiagnosticCode::NotFound : DiagnosticCode::Failed,
            missing ? "animation file not found" : "animation file cannot be opened", "collapseAnimation"));
    }
    const auto fileBytes = file.tellg();
    if (fileBytes < 20 || fileBytes > MaxFileBytes)
        return Result<CollapseAnimation>::failure(invalid("invalid animation file size"));
    file.seekg(0);
    std::string encoded(static_cast<std::size_t>(fileBytes), '\0');
    if (!file.read(encoded.data(), static_cast<std::streamsize>(encoded.size())) || file.peek() != EOF)
        return Result<CollapseAnimation>::failure(invalid("animation changed or read failed"));
    std::istringstream in(std::move(encoded), std::ios::binary);
    const auto         encodedView = in.view();
    std::streamoff     bytes       = fileBytes;
    char               magic[8];
    in.read(magic, 8);
    const auto version = read32(in), bones = read32(in), frames = read32(in);
    if (std::string(magic, 8) != "EVECOLL1" || (version < 1 || version > 4) || bones == 0 || frames == 0 ||
        bones > MaxSamples / frames)
        return Result<CollapseAnimation>::failure(invalid("unsupported schema, counts or trailing data"));
    if (version >= 3) {
        if (encodedView.size() < 56 ||
            contentKey(encodedView.substr(0, encodedView.size() - 32)) != encodedView.substr(encodedView.size() - 32))
            return Result<CollapseAnimation>::failure(invalid("animation content checksum mismatch"));
        bytes -= 32;
    }
    CollapseAnimation clip;
    if (version >= 2) {
        const auto meshes = read32(in);
        if (version == 4) {
            const auto recipeSize = read32(in);
            const auto position   = in.tellg();
            if (!in || recipeSize == 0 || recipeSize > MaxRecipeBytes || position < 0 || position > bytes ||
                static_cast<std::streamoff>(recipeSize) > bytes - position)
                return Result<CollapseAnimation>::failure(invalid("invalid bake provenance size"));
            auto decoded =
                CollapseBakeRecipe::decode(encodedView.substr(static_cast<std::size_t>(position), recipeSize));
            if (!decoded) return Result<CollapseAnimation>::failure(decoded.status());
            if (decoded.value().coordinates() != BakeCoordinates::MetresRightHandedYUp)
                return Result<CollapseAnimation>::failure(invalid("collapse requires metre-space provenance"));
            clip.recipe_ = std::make_shared<const CollapseBakeRecipe>(std::move(decoded).takeValue());
            in.seekg(static_cast<std::streamoff>(recipeSize), std::ios::cur);
        }
        if (meshes != 0 && (meshes != bones || meshes > 4096))
            return Result<CollapseAnimation>::failure(invalid("mesh count mismatch"));
        if (meshes) {
            FractureGeometry geometry;
            std::size_t      total = 0;
            for (std::uint32_t m = 0; m < meshes; ++m) {
                const auto vertices = read32(in), triangles = read32(in);
                total += 3ULL * vertices + 4ULL * triangles;
                if (!in || vertices < 4 || vertices > 254 || triangles == 0 || triangles > 2730 || total > 4000000)
                    return Result<CollapseAnimation>::failure(invalid("mesh budget exceeded"));
                FractureFragment fragment;
                fragment.positions.resize(3ULL * vertices);
                fragment.indices.resize(3ULL * triangles);
                fragment.materials.resize(triangles);
                for (auto& v : fragment.positions) v = readFloat(in);
                for (auto& i : fragment.indices) i = read32(in);
                for (auto& tag : fragment.materials) tag = read32(in);
                geometry.fragments.push_back(std::move(fragment));
            }
            auto valid = geometry.validate();
            if (!valid) return Result<CollapseAnimation>::failure(valid.status());
            clip.geometry_ = std::make_shared<const FractureGeometry>(std::move(geometry));
        }
    }
    if (!in ||
        bytes != in.tellg() + static_cast<std::streamoff>(static_cast<std::uint64_t>(frames) * (4ULL + 52ULL * bones)))
        return Result<CollapseAnimation>::failure(invalid("truncated animation or trailing bytes"));
    for (std::uint32_t f = 0; f < frames; ++f) {
        Frame frame{readFloat(in), {}};
        if (!std::isfinite(frame.seconds) || (f == 0 ? frame.seconds != 0 : frame.seconds <= clip.duration()))
            return Result<CollapseAnimation>::failure(invalid("invalid frame time"));
        if (clip.recipe_) {
            auto timing = clip.recipe_->validateSampleTime(f, frame.seconds);
            if (!timing) return Result<CollapseAnimation>::failure(timing.status());
        }
        frame.bones.reserve(bones);
        for (std::uint32_t i = 0; i < bones; ++i) {
            BonePresentation b;
            float*           values[] = {&b.halfExtentX, &b.halfExtentY, &b.halfExtentZ, &b.worldX, &b.worldY,
                                         &b.worldZ,      &b.rotX,        &b.rotY,        &b.rotZ,   &b.rotW};
            for (auto* v : values) *v = readFloat(in);
            const auto state = read32(in), cluster = read32(in), level = read32(in);
            if (state > 2 || cluster > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
                level > static_cast<std::uint32_t>(std::numeric_limits<int>::max()))
                return Result<CollapseAnimation>::failure(invalid("invalid fragment metadata"));
            b.state         = static_cast<BoneRuntimeState>(state);
            b.clusterId     = static_cast<int>(cluster);
            b.fractureLevel = static_cast<int>(level);
            if (!in || !valid(b)) return Result<CollapseAnimation>::failure(invalid("invalid fragment data"));
            if (f > 0) {
                const auto& a = clip.frames_.front().bones[i];
                if (a.halfExtentX != b.halfExtentX || a.halfExtentY != b.halfExtentY ||
                    a.halfExtentZ != b.halfExtentZ || a.clusterId != b.clusterId || a.fractureLevel != b.fractureLevel)
                    return Result<CollapseAnimation>::failure(invalid("inconsistent fragment topology"));
            }
            frame.bones.push_back(b);
        }
        clip.frames_.push_back(std::move(frame));
    }
    return Result<CollapseAnimation>::success(std::move(clip));
}
}  // namespace eve::physics
