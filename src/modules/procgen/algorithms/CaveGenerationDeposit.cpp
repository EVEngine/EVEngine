#include "procgen/algorithms/CaveGenerationState.h"

namespace eve::procgen::cave_detail {

Result<void> State::deposit() {
    // Deposits are generated from stable seeded candidates, but attach only after all
    // wall-retreat stages have produced the final host surface. This prevents the old
    // analytic chamber boundary from leaving calcite floating in evolved cave air.
    voxelSize              = 2.f / float(std::min({nx - 1, ny - 1, nz - 1}));
    embedDepth             = voxelSize * 1.15f;
    minimumFeatureRadius   = voxelSize * 0.82f;
    rejectedDripstonePairs = 0;
    anchoredDripstonePairs = 0;

    anchoredDripstones.reserve(dripstones.size());
    for (size_t i = 0; i + 1 < dripstones.size(); i += 2) {
        const Dripstone& ceilingCandidate = dripstones[i];
        const Dripstone& floorCandidate   = dripstones[i + 1];
        const float      preferredY       = (ceilingCandidate.start.y + floorCandidate.start.y) * 0.5f;
        const auto       span =
            findCaveVerticalSpan(density, nx, ny, nz, ceilingCandidate.start.x, ceilingCandidate.start.z, preferredY);
        if (!span) {
            ++rejectedDripstonePairs;
            continue;
        }
        const float gap    = span->ceiling.position.y - span->floor.position.y;
        bool        column = ceilingCandidate.endRadius > ceilingCandidate.startRadius * 0.5f &&
                      floorCandidate.endRadius > floorCandidate.startRadius * 0.5f;
        if (column) {
            const float clearanceRadius = std::max(ceilingCandidate.startRadius, floorCandidate.startRadius) * 1.18f;
            for (int sampleIndex = 1; sampleIndex < 12 && column; ++sampleIndex) {
                const float sampleY = span->floor.position.y + gap * float(sampleIndex) / 12.f;
                for (int angleIndex = 0; angleIndex < 8; ++angleIndex) {
                    const float          angle = float(angleIndex) * 0.7853981634f;
                    const CaveFieldPoint clearancePoint{ceilingCandidate.start.x + std::cos(angle) * clearanceRadius,
                                                        sampleY,
                                                        ceilingCandidate.start.z + std::sin(angle) * clearanceRadius};
                    if (sampleCaveDensity(density, nx, ny, nz, clearancePoint) > -voxelSize * 0.35f) {
                        column = false;
                        break;
                    }
                }
            }
        }
        const float maximumLength = gap * (column ? 0.515f : 0.42f);
        const float ceilingLength =
            column ? maximumLength
                   : std::min(std::abs(ceilingCandidate.end.y - ceilingCandidate.start.y), maximumLength);
        const float floorLength =
            column ? maximumLength : std::min(std::abs(floorCandidate.end.y - floorCandidate.start.y), maximumLength);
        if (ceilingLength < embedDepth * 1.5f || floorLength < embedDepth * 1.5f) {
            ++rejectedDripstonePairs;
            continue;
        }
        const Vec3 ceiling{span->ceiling.position.x, span->ceiling.position.y, span->ceiling.position.z};
        const Vec3 floor{span->floor.position.x, span->floor.position.y, span->floor.position.z};
        if (column) {
            // A mature column is one continuous implicit body. Two overlapping
            // tapered segments produced a false waist, discontinuous normals and
            // a self-shadowing ring even when their endpoints overlapped.
            anchoredDripstones.push_back({add(ceiling, {0.f, embedDepth, 0.f}), add(floor, {0.f, -embedDepth, 0.f}),
                                          std::max(ceilingCandidate.startRadius, minimumFeatureRadius),
                                          std::max(floorCandidate.startRadius, minimumFeatureRadius)});
            anchoredDripstones.back().profileAmplitude = 0.055f;
            anchoredDripstones.back().profileFrequency = 2.5f;
            anchoredDripstones.back().profilePhase =
                std::fmod(std::abs(ceiling.x * 19.7f + ceiling.z * 31.1f), 6.283185307f);
            ++anchoredDripstonePairs;
            continue;
        }
        anchoredDripstones.push_back({add(ceiling, {0.f, embedDepth, 0.f}), add(ceiling, {0.f, -ceilingLength, 0.f}),
                                      std::max(ceilingCandidate.startRadius, minimumFeatureRadius),
                                      std::max(ceilingCandidate.endRadius, minimumFeatureRadius * 0.24f)});
        anchoredDripstones.back().profileAmplitude = 0.08f;
        anchoredDripstones.back().profileFrequency = 1.5f;
        anchoredDripstones.back().profilePhase =
            std::fmod(std::abs(ceiling.x * 17.3f + ceiling.z * 23.9f), 6.283185307f);
        anchoredDripstones.push_back({add(floor, {0.f, -embedDepth, 0.f}), add(floor, {0.f, floorLength, 0.f}),
                                      std::max(floorCandidate.startRadius, minimumFeatureRadius),
                                      std::max(floorCandidate.endRadius, minimumFeatureRadius * 0.30f)});
        anchoredDripstones.back().profileAmplitude = 0.11f;
        anchoredDripstones.back().profileFrequency = 2.f;
        anchoredDripstones.back().profilePhase = std::fmod(std::abs(floor.x * 29.3f + floor.z * 13.7f), 6.283185307f);
        ++anchoredDripstonePairs;
    }
    dripstones = std::move(anchoredDripstones);

    rejectedFlowstones = 0;

    anchoredFlowstones.reserve(flowstones.size());
    for (Flowstone flowstone : flowstones) {
        const auto anchor = projectToFinalCaveSurface(
            density, nx, ny, nz, {flowstone.center.x, flowstone.center.y, flowstone.center.z}, 0.24f);
        if (!anchor || std::abs(anchor->rockNormal.y) > 0.82f) {
            ++rejectedFlowstones;
            continue;
        }
        flowstone.normal = {anchor->rockNormal.x, anchor->rockNormal.y, anchor->rockNormal.z};
        Vec3        tangent{-flowstone.normal.z, 0.f, flowstone.normal.x};
        const float tangentLength = std::sqrt(dot(tangent, tangent));
        if (tangentLength < 1e-4f) {
            ++rejectedFlowstones;
            continue;
        }
        flowstone.tangent = mul(tangent, 1.f / tangentLength);
        flowstone.center =
            add({anchor->position.x, anchor->position.y, anchor->position.z}, mul(flowstone.normal, embedDepth));
        flowstone.thickness = std::max(flowstone.thickness, minimumFeatureRadius);
        anchoredFlowstones.push_back(flowstone);
    }
    flowstones = std::move(anchoredFlowstones);

    rejectedCurtains = 0;

    anchoredCurtains.reserve(curtains.size());
    for (Curtain curtain : curtains) {
        const auto span =
            findCaveVerticalSpan(density, nx, ny, nz, curtain.anchor.x, curtain.anchor.z, curtain.anchor.y);
        if (!span || span->ceiling.rockNormal.y < 0.35f) {
            ++rejectedCurtains;
            continue;
        }
        curtain.anchor    = {span->ceiling.position.x, span->ceiling.position.y + embedDepth, span->ceiling.position.z};
        curtain.length    = std::min(curtain.length, (span->ceiling.position.y - span->floor.position.y) * 0.36f);
        curtain.thickness = std::max(curtain.thickness, minimumFeatureRadius);
        if (curtain.length < embedDepth * 2.f) {
            ++rejectedCurtains;
            continue;
        }
        anchoredCurtains.push_back(curtain);
    }
    curtains     = std::move(anchoredCurtains);
    depositDelta = std::vector<float>(density.size(), 0.f);
    for (int z = 0; z < nz; ++z) {
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const Vec3  p{float(x) / float(nx - 1) * 2.f - 1.f, float(y) / float(ny - 1) * 2.f - 1.f,
                             float(z) / float(nz - 1) * 2.f - 1.f};
                float&      d = density[size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny)];
                const float densityBeforeDeposits = d;
                for (const Dripstone& dripstone : dripstones)
                    d = smoothMaximum(d, -taperedSegmentDistance(p, dripstone, width / height, depth / height), 0.010f);
                for (const Flowstone& flowstone : flowstones)
                    d = smoothMaximum(d, -flowstoneDistance(p, flowstone), 0.008f);
                for (const Curtain& curtain : curtains) d = smoothMaximum(d, -curtainDistance(p, curtain), 0.006f);
                depositDelta[size_t(x) + size_t(y) * size_t(nx) + size_t(z) * size_t(nx) * size_t(ny)] =
                    std::max(0.f, d - densityBeforeDeposits);
                d = addCaveBreakdownBlocks(p.x, p.y, p.z, d, breakdownSet);
                d = addCaveSediment(p.x, p.y, p.z, d, sedimentSet);
            }
        }
    }


    return Result<void>::success();
}

}  // namespace eve::procgen::cave_detail
