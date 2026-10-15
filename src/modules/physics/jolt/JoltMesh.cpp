// Jolt requires its platform header before any other native header.
#include <Jolt/Jolt.h>

// Provider implementation dependencies.
#include <Jolt/Geometry/Indexify.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include "physics/jolt/JoltInternal.h"
namespace eve::physics::jolt {
eve::Result<JPH::RefConst<JPH::Shape>> cookMesh(const ShapeGeometry3D& g, JPH::uint64 userData) {
    using R = eve::Result<JPH::RefConst<JPH::Shape>>;
    JPH::MeshShapeSettings settings;
    settings.mUserData            = userData;
    settings.mPerTriangleUserData = true;
    if (g.kind == ShapeKind3D::TriangleMesh) {
        if (g.meshVertices.size() < 9 || g.meshVertices.size() % 3 || g.meshIndices.empty() ||
            g.meshIndices.size() % 3 || g.meshVertices.size() / 3 > 1000000 || g.meshIndices.size() / 3 > 2000000 ||
            !std::isfinite(g.meshWeldTolerance) || g.meshWeldTolerance < 0 ||
            (!g.meshMaterialIndices.empty() && g.meshMaterialIndices.size() != g.meshIndices.size() / 3))
            return rejected<JPH::RefConst<JPH::Shape>>("Invalid mesh source", eve::DiagnosticCode::InvalidArgument);
        for (size_t i = 0; i < g.meshVertices.size(); i += 3) {
            if (!isFinite(g.meshVertices[i], g.meshVertices[i + 1], g.meshVertices[i + 2]))
                return rejected<JPH::RefConst<JPH::Shape>>("Non-finite vertex", eve::DiagnosticCode::InvalidArgument);
            settings.mTriangleVertices.emplace_back(g.meshVertices[i], g.meshVertices[i + 1], g.meshVertices[i + 2]);
        }
        for (size_t i = 0; i < g.meshIndices.size(); i += 3) {
            for (size_t j = 0; j < 3; ++j)
                if (g.meshIndices[i + j] < 0 || size_t(g.meshIndices[i + j]) >= settings.mTriangleVertices.size())
                    return rejected<JPH::RefConst<JPH::Shape>>("Invalid triangle index",
                                                               eve::DiagnosticCode::InvalidArgument);
            JPH::IndexedTriangle triangle(g.meshIndices[i], g.meshIndices[i + 1], g.meshIndices[i + 2], 0,
                                          static_cast<JPH::uint32>(i / 3));
            if (triangle.IsDegenerate(settings.mTriangleVertices))
                return rejected<JPH::RefConst<JPH::Shape>>("Degenerate triangle", eve::DiagnosticCode::InvalidArgument);
            settings.mIndexedTriangles.push_back(triangle);
        }
        if (g.meshWeldVertices) {
            JPH::TriangleList triangles;
            for (const auto& t : settings.mIndexedTriangles)
                triangles.emplace_back(settings.mTriangleVertices[t.mIdx[0]], settings.mTriangleVertices[t.mIdx[1]],
                                       settings.mTriangleVertices[t.mIdx[2]], 0, t.mUserData);
            settings.mTriangleVertices.clear();
            settings.mIndexedTriangles.clear();
            JPH::Indexify(triangles, settings.mTriangleVertices, settings.mIndexedTriangles, g.meshWeldTolerance);
            if (settings.mIndexedTriangles.size() != triangles.size())
                return rejected<JPH::RefConst<JPH::Shape>>("Welding removed degenerate triangles",
                                                           eve::DiagnosticCode::InvalidArgument);
            for (const auto& t : settings.mIndexedTriangles)
                if (t.IsDegenerate(settings.mTriangleVertices))
                    return rejected<JPH::RefConst<JPH::Shape>>("Welding collapsed a triangle",
                                                               eve::DiagnosticCode::InvalidArgument);
        }
        settings.mActiveEdgeCosThresholdAngle = g.meshIdentifyEdges ? 0.996195f : -1.f;
        settings.mBuildQuality = g.meshUseMedianSplit ? JPH::MeshShapeSettings::EBuildQuality::FavorBuildSpeed
                                                      : JPH::MeshShapeSettings::EBuildQuality::FavorRuntimePerformance;
    } else {
        const size_t count = size_t(g.heightCountX) * size_t(g.heightCountZ);
        if (g.heightCountX < 2 || g.heightCountZ < 2 || count > 16000000 || g.heightValues.size() != count ||
            !isFinite(g.heightCellSizeX, g.heightCellSizeZ, g.heightGlobalMin) || !std::isfinite(g.heightGlobalMax) ||
            g.heightCellSizeX <= 0 || g.heightCellSizeZ <= 0 || g.heightGlobalMin > g.heightGlobalMax)
            return rejected<JPH::RefConst<JPH::Shape>>("Invalid height field", eve::DiagnosticCode::InvalidArgument);
        // Preserve rectangular grids, the fixed cell diagonal and winding of the portable contract.
        // Jolt HeightFieldShape requires a square grid; a native mesh preserves all accepted source grids.
        for (int z = 0; z < g.heightCountZ; ++z)
            for (int x = 0; x < g.heightCountX; ++x) {
                const float h  = g.heightValues[size_t(z) * g.heightCountX + x];
                const float px = x * g.heightCellSizeX, pz = z * g.heightCellSizeZ;
                if (!isFinite(px, h, pz) || h < g.heightGlobalMin || h > g.heightGlobalMax)
                    return rejected<JPH::RefConst<JPH::Shape>>("Invalid height sample",
                                                               eve::DiagnosticCode::InvalidArgument);
                settings.mTriangleVertices.emplace_back(px, h, pz);
            }
        for (int z = 0; z < g.heightCountZ - 1; ++z)
            for (int x = 0; x < g.heightCountX - 1; ++x) {
                const auto a = static_cast<JPH::uint32>(z * g.heightCountX + x), b = a + 1, c = a + g.heightCountX,
                           d = c + 1;
                auto index   = static_cast<JPH::uint32>(settings.mIndexedTriangles.size());
                settings.mIndexedTriangles.emplace_back(a, g.heightClockwise ? b : c, g.heightClockwise ? c : b, 0,
                                                        index);
                settings.mIndexedTriangles.emplace_back(d, g.heightClockwise ? c : b, g.heightClockwise ? b : c, 0,
                                                        index + 1);
            }
    }
    auto result = settings.Create();
    if (result.HasError())
        return rejected<JPH::RefConst<JPH::Shape>>(result.GetError().c_str(), eve::DiagnosticCode::InvalidArgument);
    return R::success(result.Get());
}
int triangleIndex(const JPH::Body& body, const JPH::SubShapeID& id) {
    JPH::SubShapeID remainder;
    const auto*     leaf = body.GetShape()->GetLeafShape(id, remainder);
    return leaf && leaf->GetSubType() == JPH::EShapeSubType::Mesh
               ? static_cast<int>(static_cast<const JPH::MeshShape*>(leaf)->GetTriangleUserData(remainder))
               : -1;
}
SurfaceMaterial3D contactMaterial(const JPH::Body& body, const JPH::SubShapeID& id, const ShapeState& shape) {
    const int index = triangleIndex(body, id);
    if (index >= 0 && !shape.meshMaterials.empty()) {
        const auto&  indices = shape.geometry.meshMaterialIndices;
        const size_t slot    = indices.empty() ? 0 : indices.at(size_t(index));
        return shape.meshMaterials.at(slot);
    }
    return shape.material;
}
}  // namespace eve::physics::jolt
