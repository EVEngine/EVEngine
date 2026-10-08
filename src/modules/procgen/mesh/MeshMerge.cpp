#include "procgen/mesh/MeshMerge.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>

namespace eve::procgen {
namespace {

Result<MeshBuild> weldMeshLocal(const MeshBuild& input, float tolerance,
                                std::vector<std::int32_t>& vertexSourceIds) {
    if (!std::isfinite(tolerance) || tolerance <= 0.f)
        return Result<MeshBuild>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                            "merge weld tolerance must be finite and positive",
                                                            "weldTolerance", {}, "procgen.mesh.merge"));
    struct Key {
        std::int64_t x, y, z;
        bool         operator==(const Key&) const = default;
    };
    struct Hash {
        std::size_t operator()(const Key& key) const noexcept {
            std::size_t value = std::hash<std::int64_t>{}(key.x);
            value ^= std::hash<std::int64_t>{}(key.y) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
            value ^= std::hash<std::int64_t>{}(key.z) + 0x9e3779b9u + (value << 6u) + (value >> 2u);
            return value;
        }
    };
    MeshBuild output;
    output.reserve(input.getVertexCount(), input.getIndexCount());
    std::unordered_map<Key, std::uint32_t, Hash> vertices;
    std::vector<std::uint32_t>                   remap(static_cast<std::size_t>(input.getVertexCount()));
    std::vector<std::int32_t>                    outSources;
    outSources.reserve(static_cast<std::size_t>(input.getVertexCount()));
    for (int i = 0; i < input.getVertexCount(); ++i) {
        const Key key{std::llround(input.getPositionX(i) / tolerance),
                      std::llround(input.getPositionY(i) / tolerance),
                      std::llround(input.getPositionZ(i) / tolerance)};
        const auto found = vertices.find(key);
        if (found != vertices.end()) {
            remap[static_cast<std::size_t>(i)] = found->second;
            continue;
        }
        const auto next = static_cast<std::uint32_t>(output.getVertexCount());
        vertices.emplace(key, next);
        remap[static_cast<std::size_t>(i)] = next;
        output.addVertex(input.getPositionX(i), input.getPositionY(i), input.getPositionZ(i), input.getNormalX(i),
                         input.getNormalY(i), input.getNormalZ(i), input.getUvU(i), input.getUvV(i));
        outSources.push_back(vertexSourceIds[static_cast<std::size_t>(i)]);
    }
    std::vector<int> assignments;
    for (int i = 0; i + 2 < input.getIndexCount(); i += 3) {
        const auto a = remap[static_cast<std::size_t>(input.getIndex(i))];
        const auto b = remap[static_cast<std::size_t>(input.getIndex(i + 1))];
        const auto c = remap[static_cast<std::size_t>(input.getIndex(i + 2))];
        if (a == b || b == c || a == c) continue;
        output.addTriangle(a, b, c);
        assignments.push_back(input.getTriangleGroup(i / 3));
    }
    std::vector<std::string> names;
    for (int i = 0; i < input.getGroupCount(); ++i) names.push_back(input.getGroupName(i));
    auto restored = output.restoreGroupData(std::move(names), std::move(assignments), -1);
    if (!restored.ok()) return Result<MeshBuild>::failure(restored.status());
    if (input.hasVertexColors()) {
        std::vector<float> colors(static_cast<std::size_t>(output.getVertexCount()) * 4u, 1.f);
        for (int i = 0; i < input.getVertexCount(); ++i) {
            const auto dst = remap[static_cast<std::size_t>(i)];
            for (int c = 0; c < 4; ++c)
                colors[static_cast<std::size_t>(dst) * 4u + static_cast<std::size_t>(c)] = input.getColor(i, c);
        }
        auto set = output.setVertexColors(std::move(colors));
        if (!set.ok()) return Result<MeshBuild>::failure(set.status());
    }
    for (const auto& [key, value] : input.metadata()) output.setMeta(key, value);
    vertexSourceIds = std::move(outSources);
    return Result<MeshBuild>::success(std::move(output));
}

void translateMesh(MeshBuild& mesh, float dx, float dy, float dz) {
    auto& positions = mesh.positions();
    for (std::size_t i = 0; i + 2 < positions.size(); i += 3u) {
        positions[i] += dx;
        positions[i + 1u] += dy;
        positions[i + 2u] += dz;
    }
}

}  // namespace

Result<void> MeshMergePlan::appendSource(const MeshBuild& mesh, float tx, float ty, float tz, float yawDegrees,
                                         float sx, float sy, float sz, std::string defaultMaterialId) {
    if (mesh.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "merge source mesh is empty",
                                                       "source", {}, "procgen.mesh.merge"));
    if (!std::isfinite(tx) || !std::isfinite(ty) || !std::isfinite(tz) || !std::isfinite(yawDegrees) ||
        !std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) || sx == 0.f || sy == 0.f || sz == 0.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "merge source transform is invalid", "transform", {},
                                                       "procgen.mesh.merge"));
    if (defaultMaterialId.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "merge default material id is empty", "defaultMaterialId", {},
                                                       "procgen.mesh.merge"));
    MeshBuild transformed;
    if (!transformed.appendTransformed(&mesh, tx, ty, tz, yawDegrees, sx, sy, sz))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "merge failed to transform source mesh", "source", {},
                                                       "procgen.mesh.merge"));
    if (transformed.getGroupCount() == 0) {
        // Stamp default material onto every triangle.
        MeshBuild stamped;
        stamped.reserve(transformed.getVertexCount(), transformed.getIndexCount());
        stamped.setActiveGroup(defaultMaterialId);
        for (int v = 0; v < transformed.getVertexCount(); ++v)
            stamped.addVertex(transformed.getPositionX(v), transformed.getPositionY(v), transformed.getPositionZ(v),
                              transformed.getNormalX(v), transformed.getNormalY(v), transformed.getNormalZ(v),
                              transformed.getUvU(v), transformed.getUvV(v));
        for (int i = 0; i + 2 < transformed.getIndexCount(); i += 3)
            stamped.addTriangle(static_cast<std::uint32_t>(transformed.getIndex(i)),
                                static_cast<std::uint32_t>(transformed.getIndex(i + 1)),
                                static_cast<std::uint32_t>(transformed.getIndex(i + 2)));
        if (transformed.hasVertexColors()) {
            auto set = stamped.setVertexColors(transformed.colors());
            if (!set.ok()) return Result<void>::failure(set.status());
        }
        transformed = std::move(stamped);
    }
    Source source;
    source.mesh              = std::move(transformed);
    source.defaultMaterialId = std::move(defaultMaterialId);
    sources_.push_back(std::move(source));
    return Result<void>::success();
}

void MeshMergePlan::clear() noexcept { sources_.clear(); }
int  MeshMergePlan::getSourceCount() const noexcept { return static_cast<int>(sources_.size()); }

Result<void> MeshMergePlan::setEnableContactBlend(bool enabled) noexcept {
    enableContactBlend_ = enabled;
    return Result<void>::success();
}

Result<void> MeshMergePlan::setContactBlendParams(const MeshContactBlendParams& params) {
    if (params.falloff != "smooth" && params.falloff != "linear" && params.falloff != "sharp" &&
        params.falloff != "sphere")
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "merge contact blend falloff is invalid", "falloff", {},
                                                       "procgen.mesh.merge"));
    falloffStorage_      = std::string(params.falloff);
    blendParams_         = params;
    blendParams_.falloff = falloffStorage_;
    return Result<void>::success();
}

Result<void> MeshMergePlan::setWeldTolerance(float tolerance) noexcept {
    if (!std::isfinite(tolerance))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "merge weld tolerance must be finite", "weldTolerance", {},
                                                       "procgen.mesh.merge"));
    weldTolerance_ = tolerance;
    return Result<void>::success();
}

Result<void> MeshMergePlan::setSimplifyQuality(float quality) noexcept {
    if (!std::isfinite(quality) || quality > 1.f)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "merge simplify quality must be finite and <= 1",
                                                       "simplifyQuality", {}, "procgen.mesh.merge"));
    simplifyQuality_ = quality;
    return Result<void>::success();
}

Result<void> MeshMergePlan::setPivotMode(PivotMode mode) noexcept {
    pivotMode_ = mode;
    return Result<void>::success();
}

Result<void> MeshMergePlan::setMergeVertexColors(bool enabled) noexcept {
    mergeVertexColors_ = enabled;
    return Result<void>::success();
}

Result<MeshBuild> mergeStaticMeshes(const MeshMergePlan& plan) {
    if (plan.sources_.empty())
        return Result<MeshBuild>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "merge plan has no sources", "plan", {}, "procgen.mesh.merge"));

    MeshBuild                 combined;
    std::vector<std::int32_t> vertexSourceIds;
    float                     firstX = 0.f, firstY = 0.f, firstZ = 0.f;
    bool                      haveFirst = false;

    for (std::size_t sourceIndex = 0; sourceIndex < plan.sources_.size(); ++sourceIndex) {
        const auto& source = plan.sources_[sourceIndex];
        if (!haveFirst && source.mesh.getVertexCount() > 0) {
            firstX    = source.mesh.getPositionX(0);
            firstY    = source.mesh.getPositionY(0);
            firstZ    = source.mesh.getPositionZ(0);
            haveFirst = true;
        }
        if (!combined.appendTransformed(&source.mesh, 0.f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f))
            return Result<MeshBuild>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "merge append failed", "source", {}, "procgen.mesh.merge"));
        for (int v = 0; v < source.mesh.getVertexCount(); ++v)
            vertexSourceIds.push_back(static_cast<std::int32_t>(sourceIndex));
    }

    if (!plan.mergeVertexColors_ && combined.hasVertexColors()) {
        auto cleared = combined.setVertexColors({});
        if (!cleared.ok()) return Result<MeshBuild>::failure(cleared.status());
    }

    if (plan.weldTolerance_ > 0.f) {
        auto welded = weldMeshLocal(combined, plan.weldTolerance_, vertexSourceIds);
        if (!welded.ok()) return welded;
        combined = std::move(welded).takeValue();
    }

    if (plan.enableContactBlend_) {
        auto blended = meshContactBlendResult(combined, vertexSourceIds, plan.blendParams_);
        if (!blended.ok()) return blended;
        combined = std::move(blended).takeValue();
    }

    if (plan.simplifyQuality_ > 0.f && plan.simplifyQuality_ < 1.f) {
        MeshBuild simplified;
        auto      simp = simplifyGtsMesh(simplified, combined, plan.simplifyQuality_, {});
        if (!simp.ok()) return Result<MeshBuild>::failure(simp.status());
        combined = std::move(simplified);
    }

    if (plan.pivotMode_ == MeshMergePlan::PivotMode::FirstSource && haveFirst)
        translateMesh(combined, -firstX, -firstY, -firstZ);

    combined.setMeta("kind", "procgen.mesh.merged");
    combined.setMeta("contactBlend.enabled", plan.enableContactBlend_ ? "1" : "0");
    return Result<MeshBuild>::success(std::move(combined));
}

}  // namespace eve::procgen
