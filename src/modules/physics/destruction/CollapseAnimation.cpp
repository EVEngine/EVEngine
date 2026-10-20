#include "physics/destruction/CollapseAnimation.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include "physics/destruction/FractureGeometry.h"

namespace eve::physics {
std::string CollapseAnimation::contentKey(std::string_view source) {
    std::uint64_t a = 14695981039346656037ull, b = 0x9e3779b97f4a7c15ull;
    for (unsigned char c : source) {
        a = (a ^ c) * 1099511628211ull;
        b = (b ^ c) * 0x100000001b3ull;
        b ^= b >> 29;
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string    key(32, '0');
    for (unsigned i = 0; i < 16; ++i) {
        key[i]      = digits[(a >> ((15 - i) * 4)) & 15];
        key[16 + i] = digits[(b >> ((15 - i) * 4)) & 15];
    }
    return key;
}

namespace {
constexpr std::uint32_t MaxSamples = 2000000;
Diagnostic              invalid(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "collapseAnimation");
}
bool valid(const BonePresentation& b) {
    const float values[] = {b.halfExtentX, b.halfExtentY, b.halfExtentZ, b.worldX, b.worldY,
                            b.worldZ,      b.rotX,        b.rotY,        b.rotZ,   b.rotW};
    for (float v : values)
        if (!std::isfinite(v)) return false;
    const float norm = b.rotX * b.rotX + b.rotY * b.rotY + b.rotZ * b.rotZ + b.rotW * b.rotW;
    return b.halfExtentX > 0 && b.halfExtentY > 0 && b.halfExtentZ > 0 && std::isfinite(norm) && norm > 0.5f &&
           norm < 1.5f && static_cast<unsigned>(b.state) <= 2 && b.clusterId >= 0 && b.fractureLevel >= 0;
}
void write32(std::ostream& out, std::uint32_t v) {
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
std::shared_ptr<const FractureGeometry> CollapseAnimation::fragmentGeometry() const noexcept { return geometry_; }
float CollapseAnimation::duration() const noexcept { return frames_.empty() ? 0 : frames_.back().seconds; }
int   CollapseAnimation::boneCount() const noexcept {
    return frames_.empty() ? 0 : static_cast<int>(frames_.front().bones.size());
}
int          CollapseAnimation::frameCount() const noexcept { return static_cast<int>(frames_.size()); }
Result<void> CollapseAnimation::capture(const GeometryCollectionInstance& source, float seconds) {
    const auto sourceGeometry = source.fragmentGeometry();
    if (!frames_.empty() && sourceGeometry != geometry_)
        return Result<void>::failure(invalid("recording source geometry changed"));
    const int count = source.boneCount();
    if (!std::isfinite(seconds) || (frames_.empty() ? seconds != 0 : seconds <= duration()) || count <= 0 ||
        (!frames_.empty() && count != boneCount()) || (frames_.size() + 1) * static_cast<size_t>(count) > MaxSamples)
        return Result<void>::failure(invalid("invalid capture time, topology or sample budget"));
    Frame frame{seconds, {}};
    frame.bones.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        auto pose = source.bonePresentation(i);
        if (!pose) return Result<void>::failure(pose.status());
        if (!valid(pose.value())) return Result<void>::failure(invalid("invalid fragment pose"));
        if (!frames_.empty()) {
            const auto& a = frames_.front().bones[static_cast<size_t>(i)];
            const auto& b = pose.value();
            if (a.halfExtentX != b.halfExtentX || a.halfExtentY != b.halfExtentY || a.halfExtentZ != b.halfExtentZ ||
                a.clusterId != b.clusterId || a.fractureLevel != b.fractureLevel)
                return Result<void>::failure(invalid("fragment topology changed while recording"));
        }
        frame.bones.push_back(pose.value());
    }
    if (frames_.empty()) geometry_ = sourceGeometry;
    frames_.push_back(std::move(frame));
    return Result<void>::success();
}
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
    std::error_code error;
    if (frames_.empty() || path.empty() || std::filesystem::exists(std::filesystem::path(path), error) || error)
        return Result<void>::failure(invalid("save requires frames and a new writable path"));
    std::ofstream out(std::filesystem::path(path), std::ios::binary);
    out.write("EVECOLL1", 8);
    write32(out, 2);
    write32(out, static_cast<std::uint32_t>(boneCount()));
    write32(out, static_cast<std::uint32_t>(frameCount()));
    write32(out, geometry_ ? static_cast<std::uint32_t>(geometry_->fragments.size()) : 0U);
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
    out.close();
    if (!out) {
        std::filesystem::remove(std::filesystem::path(path), error);
        return Result<void>::failure(invalid("animation file write failed"));
    }
    return Result<void>::success();
}
Result<CollapseAnimation> CollapseAnimation::load(std::string_view path) {
    std::ifstream in(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    const auto    bytes = in.tellg();
    if (bytes < 20 || bytes > static_cast<std::streamoff>(MaxSamples) * 56 + 16000024)
        return Result<CollapseAnimation>::failure(invalid("invalid animation file size"));
    in.seekg(0);
    char magic[8];
    in.read(magic, 8);
    const auto version = read32(in), bones = read32(in), frames = read32(in);
    if (std::string(magic, 8) != "EVECOLL1" || (version != 1 && version != 2) || bones == 0 || frames == 0 ||
        bones > MaxSamples / frames)
        return Result<CollapseAnimation>::failure(invalid("unsupported schema, counts or trailing data"));
    CollapseAnimation clip;
    if (version == 2) {
        const auto meshes = read32(in);
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
