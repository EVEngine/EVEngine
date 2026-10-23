#include "physics/box3d/Box3DShapeCook.h"
#include <cmath>
#include "common/Exception.h"
namespace eve::physics::box3d {
b3HullData* createCheckedHull(const std::vector<float>& vertices, int maxVertices, const char* operation) {
    if (vertices.size() < 12 || vertices.size() % 3 != 0)
        throw eve::Exception("%s: vertices must contain at least four packed XYZ points", operation);
    if (vertices.size() / 3 > 100000) throw eve::Exception("%s: source point count must be <= 100000", operation);
    if (maxVertices < 4 || maxVertices > 254) throw eve::Exception("%s: maxVertices must be in [4, 254]", operation);
    std::vector<b3Vec3> points;
    points.reserve(vertices.size() / 3);
    for (size_t i = 0; i < vertices.size(); i += 3) {
        if (!std::isfinite(vertices[i]) || !std::isfinite(vertices[i + 1]) || !std::isfinite(vertices[i + 2]))
            throw eve::Exception("%s: all vertex components must be finite", operation);
        points.push_back({vertices[i], vertices[i + 1], vertices[i + 2]});
    }
    b3HullData* hull = b3CreateHull(points.data(), static_cast<int>(points.size()), maxVertices);
    if (!hull) throw eve::Exception("%s: points do not form a valid three-dimensional convex hull", operation);
    return hull;
}

b3MeshData* createCheckedMesh(const std::vector<float>& vertices, const std::vector<int32_t>& indices,
                              bool weldVertices, float weldTolerance, bool identifyEdges, bool useMedianSplit,
                              b3Transform transform, const std::vector<uint8_t>* materialIndices,
                              const char* operation) {
    if (vertices.size() < 9 || vertices.size() % 3 != 0)
        throw eve::Exception("%s: vertices must contain at least three packed XYZ points", operation);
    if (indices.size() < 3 || indices.size() % 3 != 0)
        throw eve::Exception("%s: indices must contain complete triangles", operation);
    if (vertices.size() / 3 > 1000000 || indices.size() / 3 > 2000000)
        throw eve::Exception("%s: mesh exceeds 1000000 vertices or 2000000 triangles", operation);
    if (!std::isfinite(weldTolerance) || weldTolerance < 0.f)
        throw eve::Exception("%s: weldTolerance must be finite and >= 0", operation);
    std::vector<b3Vec3> points;
    points.reserve(vertices.size() / 3);
    for (size_t i = 0; i < vertices.size(); i += 3) {
        if (!std::isfinite(vertices[i]) || !std::isfinite(vertices[i + 1]) || !std::isfinite(vertices[i + 2]))
            throw eve::Exception("%s: all vertex components must be finite", operation);
        const b3Vec3 point{vertices[i], vertices[i + 1], vertices[i + 2]};
        points.push_back(transform.p + b3RotateVector(transform.q, point));
    }
    for (int32_t index : indices) {
        if (index < 0 || static_cast<size_t>(index) >= points.size())
            throw eve::Exception("%s: triangle index is outside the vertex array", operation);
    }
    std::vector<int32_t> mutableIndices = indices;
    b3MeshDef            def{};
    def.vertices = points.data();
    def.indices  = mutableIndices.data();
    def.materialIndices =
        materialIndices && !materialIndices->empty() ? const_cast<uint8_t*>(materialIndices->data()) : nullptr;
    def.vertexCount    = static_cast<int>(points.size());
    def.triangleCount  = static_cast<int>(indices.size() / 3);
    def.weldVertices   = weldVertices;
    def.weldTolerance  = weldTolerance;
    def.identifyEdges  = identifyEdges;
    def.useMedianSplit = useMedianSplit;
    b3MeshData* mesh   = b3CreateMesh(&def, nullptr, 0);
    if (!mesh || mesh->triangleCount != def.triangleCount) {
        if (mesh) b3DestroyMesh(mesh);
        throw eve::Exception("%s: mesh contains degenerate or zero-area triangles", operation);
    }
    return mesh;
}

b3HeightFieldData* createCheckedHeightField(int countX, int countZ, float cellSizeX, float cellSizeZ,
                                            const std::vector<float>& heights, float globalMin, float globalMax,
                                            bool clockwiseWinding, const char* operation) {
    if (countX < 2 || countZ < 2) throw eve::Exception("%s: countX and countZ must be >= 2", operation);
    const size_t sampleCount = static_cast<size_t>(countX) * static_cast<size_t>(countZ);
    if (sampleCount > 16000000) throw eve::Exception("%s: sample count must be <= 16000000", operation);
    if (heights.size() != sampleCount) throw eve::Exception("%s: heights size must equal countX*  countZ", operation);
    if (!(cellSizeX > 0.f) || !(cellSizeZ > 0.f) || !std::isfinite(cellSizeX) || !std::isfinite(cellSizeZ))
        throw eve::Exception("%s: cell sizes must be finite and > 0", operation);
    if (!std::isfinite(globalMin) || !std::isfinite(globalMax) || globalMin > globalMax)
        throw eve::Exception("%s: global height range must be finite and ordered", operation);
    for (float height : heights) {
        if (!std::isfinite(height) || height < globalMin || height > globalMax)
            throw eve::Exception("%s: every height must be finite and inside global range", operation);
    }
    std::vector<float> mutableHeights = heights;
    b3HeightFieldDef   def{};
    def.heights             = mutableHeights.data();
    def.scale               = {cellSizeX, 1.f, cellSizeZ};
    def.countX              = countX;
    def.countZ              = countZ;
    def.globalMinimumHeight = globalMin;
    def.globalMaximumHeight = globalMax;
    def.clockwiseWinding    = clockwiseWinding;
    return b3CreateHeightField(&def);
}


}  // namespace eve::physics::box3d
