#include "procgen/GtsMeshSplitter.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace eve::procgen {
namespace {
struct Vertex {
    float x, y, z, nx, ny, nz, u, v, r, g, b, a;
};
Vertex mix(const Vertex& a, const Vertex& b, float t) {
    Vertex       r{};
    const float* pa = &a.x;
    const float* pb = &b.x;
    float*       pr = &r.x;
    for (int i = 0; i < 12; ++i) pr[i] = pa[i] + (pb[i] - pa[i]) * t;
    return r;
}
template <class Distance>
std::vector<Vertex> clip(const std::vector<Vertex>& in, Distance distance) {
    std::vector<Vertex> out;
    if (in.empty()) return out;
    out.reserve(in.size() + 1);
    Vertex previous       = in.back();
    float  dp             = distance(previous);
    bool   previousInside = dp >= 0.f;
    for (const auto& current : in) {
        const float dc            = distance(current);
        const bool  currentInside = dc >= 0.f;
        if (currentInside != previousInside) {
            const float denominator = dp - dc;
            const float t           = std::abs(denominator) > 1e-20f ? dp / denominator : 0.f;
            out.push_back(mix(previous, current, std::clamp(t, 0.f, 1.f)));
        }
        if (currentInside) out.push_back(current);
        previous       = current;
        dp             = dc;
        previousInside = currentInside;
    }
    return out;
}
bool finiteVertex(const Vertex& v) {
    const float* p = &v.x;
    for (int i = 0; i < 12; ++i) {
        if (!std::isfinite(p[i])) return false;
    }
    return true;
}
bool nonDegenerate(const Vertex& a, const Vertex& b, const Vertex& c) {
    const double abx = double(b.x) - a.x, aby = double(b.y) - a.y, abz = double(b.z) - a.z;
    const double acx = double(c.x) - a.x, acy = double(c.y) - a.y, acz = double(c.z) - a.z;
    const double nx = aby * acz - abz * acy, ny = abz * acx - abx * acz, nz = abx * acy - aby * acx;
    return nx * nx + ny * ny + nz * nz > 1e-24;
}
}  // namespace

const GtsMeshSplitTile* GtsMeshSplitResult::tileAt(int index) const noexcept {
    return index >= 0 && index < getTileCount() ? &tiles_[static_cast<std::size_t>(index)] : nullptr;
}
std::unique_ptr<MeshBuild> GtsMeshSplitResult::copyTileMesh(int index) const {
    const auto* tile = tileAt(index);
    return tile ? std::make_unique<MeshBuild>(tile->mesh) : nullptr;
}
float GtsMeshSplitResult::getTileOffsetX(int index) const noexcept {
    const auto* tile = tileAt(index);
    return tile ? tile->offsetX : 0.f;
}
float GtsMeshSplitResult::getTileOffsetZ(int index) const noexcept {
    const auto* tile = tileAt(index);
    return tile ? tile->offsetZ : 0.f;
}

Result<GtsMeshSplitResult> splitGtsMesh(const MeshBuild& source, int xSplits, int zSplits, GtsMeshPivot pivot) {
    const int vertexCount = source.getVertexCount(), indexCount = source.getIndexCount();
    if (vertexCount < 3 || indexCount < 3 || indexCount % 3 != 0 || xSplits < 0 || zSplits < 0 || xSplits > 255 ||
        zSplits > 255 || static_cast<int>(pivot) < 0 || static_cast<int>(pivot) > 2)
        return Result<GtsMeshSplitResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GTS mesh split requires a valid triangle mesh, split counts and pivot",
            "procgen.gtsMeshSplit"));
    float minX = std::numeric_limits<float>::max(), maxX = -minX, minZ = minX, maxZ = -minX;
    for (int i = 0; i < vertexCount; ++i) {
        Vertex v{source.getPositionX(i), source.getPositionY(i), source.getPositionZ(i), source.getNormalX(i),
                 source.getNormalY(i),   source.getNormalZ(i),   source.getUvU(i),       source.getUvV(i),
                 source.getColor(i, 0),  source.getColor(i, 1),  source.getColor(i, 2),  source.getColor(i, 3)};
        if (!finiteVertex(v))
            return Result<GtsMeshSplitResult>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "GTS mesh streams must contain only finite values",
                                  "procgen.gtsMeshSplit"));
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
        minZ = std::min(minZ, v.z);
        maxZ = std::max(maxZ, v.z);
    }
    for (int i = 0; i < indexCount; ++i)
        if (source.getIndex(i) < 0 || source.getIndex(i) >= vertexCount)
            return Result<GtsMeshSplitResult>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "GTS mesh index is out of range", "procgen.gtsMeshSplit"));
    if (!(maxX > minX) || !(maxZ > minZ))
        return Result<GtsMeshSplitResult>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "GTS mesh must have non-zero X/Z bounds", "procgen.gtsMeshSplit"));
    GtsMeshSplitResult result;
    result.columns_ = xSplits + 1;
    result.rows_    = zSplits + 1;
    result.tiles_.resize(static_cast<std::size_t>(result.columns_ * result.rows_));
    const float cellX = (maxX - minX) / result.columns_, cellZ = (maxZ - minZ) / result.rows_;
    for (int row = 0; row < result.rows_; ++row)
        for (int column = 0; column < result.columns_; ++column) {
            const float loX = minX + column * cellX,
                        hiX = column + 1 == result.columns_ ? maxX : minX + (column + 1) * cellX;
            const float loZ = minZ + row * cellZ, hiZ = row + 1 == result.rows_ ? maxZ : minZ + (row + 1) * cellZ;
            auto&       tile   = result.tiles_[static_cast<std::size_t>(row * result.columns_ + column)];
            float       pivotX = 0.f, pivotZ = 0.f;
            if (pivot == GtsMeshPivot::MinimumXZ) {
                pivotX = loX;
                pivotZ = loZ;
            } else if (pivot == GtsMeshPivot::CenterXZ) {
                pivotX = (loX + hiX) * .5f;
                pivotZ = (loZ + hiZ) * .5f;
            }
            tile.offsetX = pivotX;
            tile.offsetZ = pivotZ;
            std::vector<float> tileColors;
            for (int i = 0; i < indexCount; i += 3) {
                std::vector<Vertex> polygon;
                polygon.reserve(7);
                for (int k = 0; k < 3; ++k) {
                    const int n = source.getIndex(i + k);
                    polygon.push_back({source.getPositionX(n), source.getPositionY(n), source.getPositionZ(n),
                                       source.getNormalX(n), source.getNormalY(n), source.getNormalZ(n),
                                       source.getUvU(n), source.getUvV(n), source.getColor(n, 0), source.getColor(n, 1),
                                       source.getColor(n, 2), source.getColor(n, 3)});
                }
                polygon = clip(polygon, [&](const Vertex& v) { return v.x - loX; });
                polygon = clip(polygon, [&](const Vertex& v) { return hiX - v.x; });
                polygon = clip(polygon, [&](const Vertex& v) { return v.z - loZ; });
                polygon = clip(polygon, [&](const Vertex& v) { return hiZ - v.z; });
                if (polygon.size() < 3) continue;
                const int group = source.getTriangleGroup(i / 3);
                if (group >= 0)
                    tile.mesh.setActiveGroup(source.getGroupName(group));
                else
                    tile.mesh.setActiveGroup("");
                const auto add = [&](const Vertex& v) {
                    const auto n = static_cast<std::uint32_t>(tile.mesh.getVertexCount());
                    tile.mesh.addVertex(v.x - pivotX, v.y, v.z - pivotZ, v.nx, v.ny, v.nz, v.u, v.v);
                    if (source.hasVertexColors()) tileColors.insert(tileColors.end(), {v.r, v.g, v.b, v.a});
                    return n;
                };
                for (std::size_t k = 1; k + 1 < polygon.size(); ++k) {
                    if (!nonDegenerate(polygon[0], polygon[k], polygon[k + 1])) continue;
                    tile.mesh.addTriangle(add(polygon[0]), add(polygon[k]), add(polygon[k + 1]));
                }
            }
            if (!tileColors.empty())
                tile.mesh.setVertexColors(std::move(tileColors)).ignore("validated clipped color stream");
        }
    return Result<GtsMeshSplitResult>::success(std::move(result));
}
Result<void> splitGtsMeshInto(GtsMeshSplitResult& output, const MeshBuild& source, int xSplits, int zSplits,
                              GtsMeshPivot pivot) {
    auto result = splitGtsMesh(source, xSplits, zSplits, pivot);
    if (!result.ok()) return Result<void>::failure(result.status());
    output = std::move(result).value();
    return Result<void>::success();
}
}  // namespace eve::procgen
