#include "procgen/algorithms/CaveGenerationState.h"

namespace eve::procgen::cave_detail {

Result<void> State::mesh(MeshBuild& out) {
    std::string error;
    voxelSize = 2.f / float(std::min({nx - 1, ny - 1, nz - 1}));
    if (depositDelta.empty()) depositDelta.assign(density.size(), 0.f);
    detachment = detachUnsupportedCaveFragments(density, nx, ny, nz, fragmentDetachment);
    boundary   = closeCaveDensityBoundary(density, nx, ny, nz, boundaryClosure, params.getSeed());


    const std::vector<float>* extractionDensity = &density;
    extractionNx                                = nx;
    extractionNy                                = ny;
    extractionNz                                = nz;
    if (isosurfaceSampling > 1) {
        resampled         = resampleCaveDensity(density, nx, ny, nz, isosurfaceSampling);
        extractionDensity = &resampled.density;
        extractionNx      = resampled.nx;
        extractionNy      = resampled.ny;
        extractionNz      = resampled.nz;
    }
    if (!marchingCubes(extractionDensity->data(), extractionNx, extractionNy, extractionNz, 0.f, out, &error) ||
        out.empty()) {
        if (error.empty()) error = "mesh.cave: generated an empty cave";
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error, {}, {}, "procgen.cave"));
    }
    caveWallGroups = std::vector<int>(size_t(out.getIndexCount() / 3), 0);
    groupTolerance = 1.5f / float(std::min({extractionNx - 1, extractionNy - 1, extractionNz - 1}));
    for (int triangle = 0; triangle < out.getIndexCount() / 3; ++triangle) {
        Vec3 center{};
        for (int corner = 0; corner < 3; ++corner) {
            const int index = out.indices()[size_t(triangle * 3 + corner)];
            // marchingCubes emits a cube centered at the origin in [-0.5, 0.5].
            center.x += out.getPositionX(index) * 2.f;
            center.y += out.getPositionY(index) * 2.f;
            center.z += out.getPositionZ(index) * 2.f;
        }
        center = mul(center, 1.f / 3.f);
        const bool createdByDeposition =
            sampleCaveDensity(depositDelta, nx, ny, nz, {center.x, center.y, center.z}) > voxelSize * 0.01f;
        if (createdByDeposition) {
            for (const Dripstone& dripstone : dripstones) {
                if (std::fabs(taperedSegmentDistance(center, dripstone, width / height, depth / height)) <=
                    groupTolerance) {
                    caveWallGroups[size_t(triangle)] = 1;
                    break;
                }
            }
            if (caveWallGroups[size_t(triangle)] == 0) {
                for (const Flowstone& flowstone : flowstones) {
                    if (std::fabs(flowstoneDistance(center, flowstone)) <= groupTolerance) {
                        caveWallGroups[size_t(triangle)] = 1;
                        break;
                    }
                }
            }
            if (caveWallGroups[size_t(triangle)] == 0) {
                for (const Curtain& curtain : curtains) {
                    if (std::fabs(curtainDistance(center, curtain)) <= groupTolerance) {
                        caveWallGroups[size_t(triangle)] = 1;
                        break;
                    }
                }
            }
        }
        if (isCaveBreakdownBlockSurface(center.x, center.y, center.z, groupTolerance, breakdownSet))
            caveWallGroups[size_t(triangle)] = 3;
        if (isCaveSedimentSurface(center.x, center.y, center.z, groupTolerance, sedimentSet))
            caveWallGroups[size_t(triangle)] = 4;
    }
    auto groupResult = out.restoreGroupData({"caveWalls", "speleothems", "wetWalls", "breakdown", "sediment"},
                                            std::move(caveWallGroups), 0);
    if (!groupResult) {
        const auto* diagnostic = groupResult.error();
        error                  = "mesh.cave: failed to assign cave wall group";
        if (diagnostic) error += ": " + diagnostic->message();
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error, {}, {}, "procgen.cave"));
    }
    refinedSourceTriangles = 0;
    adaptiveSplitEdges     = 0;
    if (surfaceRefinement > 0) {
        MeshBuild refined;
        refined.reserve(out.getVertexCount() * 4, out.getIndexCount() * 4);
        std::unordered_map<EdgeKey, EdgeProjection, EdgeKeyHash> edgeProjections;
        edgeProjections.reserve(size_t(out.getIndexCount()));
        auto edgeProjection = [&](Vec3 a, Vec3 b) -> EdgeProjection {
            const EdgeKey key   = makeEdgeKey(a, b);
            auto          found = edgeProjections.find(key);
            if (found != edgeProjections.end()) return found->second;
            const Vec3 midpoint = mul(add(a, b), 0.5f);
            Vec3       projected =
                projectToDensitySurface(midpoint, *extractionDensity, extractionNx, extractionNy, extractionNz);
            Vec3        error             = sub(projected, midpoint);
            const float errorLength       = std::sqrt(dot(error, error));
            const float edgeLength        = std::sqrt(dot(sub(b, a), sub(b, a)));
            const float maximumProjection = edgeLength * (surfaceRefinement == 1 ? 0.35f : 0.10f);
            if (errorLength > maximumProjection && errorLength > 1e-8f) {
                error     = mul(error, maximumProjection / errorLength);
                projected = add(midpoint, error);
            }
            const bool split = surfaceRefinement == 1 || std::sqrt(dot(error, error)) >= refinementThreshold;
            if (split) ++adaptiveSplitEdges;
            return edgeProjections.emplace(key, EdgeProjection{projected, split}).first->second;
        };
        for (int triangle = 0; triangle < out.getIndexCount() / 3; ++triangle) {
            const int group = out.getTriangleGroup(triangle);
            refined.setActiveGroup(out.getGroupName(group));
            Vec3 corners[3];
            for (int corner = 0; corner < 3; ++corner) {
                const int index = out.getIndex(triangle * 3 + corner);
                corners[corner] = {out.getPositionX(index), out.getPositionY(index), out.getPositionZ(index)};
            }
            const EdgeProjection e01 = edgeProjection(corners[0], corners[1]);
            const EdgeProjection e12 = edgeProjection(corners[1], corners[2]);
            const EdgeProjection e20 = edgeProjection(corners[2], corners[0]);
            const Vec3           ab  = sub(corners[1], corners[0]);
            const Vec3           ac  = sub(corners[2], corners[0]);
            const Vec3 referenceNormal{ab.y * ac.z - ab.z * ac.y, ab.z * ac.x - ab.x * ac.z, ab.x * ac.y - ab.y * ac.x};
            if (surfaceRefinement == 1) {
                ++refinedSourceTriangles;
                addFacetedTriangle(refined, corners[0], e01.projected, e20.projected, referenceNormal);
                addFacetedTriangle(refined, e01.projected, corners[1], e12.projected, referenceNormal);
                addFacetedTriangle(refined, e20.projected, e12.projected, corners[2], referenceNormal);
                addFacetedTriangle(refined, e01.projected, e12.projected, e20.projected, referenceNormal);
                continue;
            }
            const int splitMask = (e01.split ? 1 : 0) | (e12.split ? 2 : 0) | (e20.split ? 4 : 0);
            if (splitMask != 0) ++refinedSourceTriangles;
            auto emit = [&](Vec3 a, Vec3 b, Vec3 c) { addFacetedTriangle(refined, a, b, c, referenceNormal); };
            switch (splitMask) {
                case 0: emit(corners[0], corners[1], corners[2]); break;
                case 1:
                    emit(corners[0], e01.projected, corners[2]);
                    emit(e01.projected, corners[1], corners[2]);
                    break;
                case 2:
                    emit(corners[1], e12.projected, corners[0]);
                    emit(e12.projected, corners[2], corners[0]);
                    break;
                case 3:
                    emit(corners[1], e12.projected, e01.projected);
                    emit(corners[0], e01.projected, corners[2]);
                    emit(e01.projected, e12.projected, corners[2]);
                    break;
                case 4:
                    emit(corners[2], e20.projected, corners[1]);
                    emit(e20.projected, corners[0], corners[1]);
                    break;
                case 5:
                    emit(corners[0], e01.projected, e20.projected);
                    emit(corners[2], e20.projected, corners[1]);
                    emit(e20.projected, e01.projected, corners[1]);
                    break;
                case 6:
                    emit(corners[2], e20.projected, e12.projected);
                    emit(corners[1], e12.projected, corners[0]);
                    emit(e12.projected, e20.projected, corners[0]);
                    break;
                case 7:
                    emit(corners[0], e01.projected, e20.projected);
                    emit(e01.projected, corners[1], e12.projected);
                    emit(e20.projected, e12.projected, corners[2]);
                    emit(e01.projected, e12.projected, e20.projected);
                    break;
            }
        }
        out = std::move(refined);
    }
    wetness         = refineCaveWetnessBoundary(out, spine, tunnelRadius, params.getSeed(), wetnessRefinement > 0);
    auto& positions = out.positions();
    // marchingCubes already normalizes grid coordinates to [-0.5, 0.5], so world
    // dimensions are direct axis scales.  Applying another voxel-size division would
    // collapse the cave into a tiny patch at the negative world bound.
    sx = width;
    sy = height;
    sz = depth;
    if (applyCaveSurfaceNormals(out, *extractionDensity, extractionNx, extractionNy, extractionNz, width, height, depth,
                                surfaceNormalMode, normalSmoothing, error) != CaveNormalStatus::applied)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, error, {}, {}, "procgen.cave"));
    for (size_t i = 0; i < positions.size(); i += 3) {
        positions[i] *= sx;
        positions[i + 1] *= sy;
        positions[i + 2] *= sz;
    }
    extractionVoxels = extractionDensity->size();
    metadata(out);

    return Result<void>::success();
}

}  // namespace eve::procgen::cave_detail
