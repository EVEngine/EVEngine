#include "procgen/algorithms/CaveGenerationState.h"

namespace eve::procgen::cave_detail {

Result<void> State::skeleton() {
    std::mt19937                          rng(params.getSeed());
    std::uniform_real_distribution<float> unit(-1.f, 1.f);
    std::uniform_real_distribution<float> positive(0.f, 1.f);

    spinePoints = std::max(8, chamberCount * 2);
    spine.reserve(size_t(spinePoints));
    for (int i = 0; i < spinePoints; ++i) {
        const float t = float(i) / float(spinePoints - 1);
        float       y = unit(rng) * 0.16f;
        float       z = std::sin(t * 9.f + unit(rng)) * 0.22f + unit(rng) * 0.1f;
        if (style == "vertical") y = (t - 0.5f) * 1.25f + unit(rng) * 0.12f;
        if (style == "labyrinth") z += std::sin(t * 22.f) * 0.24f;
        const float coherentRadius = 1.f + passageVariation * (std::sin(t * 4.f * 3.1415926535f + 0.7f) * 0.24f +
                                                               std::sin(t * 9.f * 3.1415926535f + 1.9f) * 0.10f);
        const float randomRadius   = 0.78f + positive(rng) * 0.55f;
        spine.push_back({{-0.72f + t * 1.44f, std::clamp(y, -0.72f, 0.72f), std::clamp(z, -0.68f, 0.68f)},
                         tunnelRadius * randomRadius * std::clamp(coherentRadius, 0.68f, 1.38f)});
    }


    branches.reserve(size_t(branchCount));
    for (int branch = 0; branch < branchCount; ++branch) {
        const int                 anchor = 1 + int(rng() % uint32_t(std::max(1, spinePoints - 2)));
        const int                 points = 3 + int(rng() % 4u);
        std::vector<PassagePoint> path;
        path.reserve(size_t(points + 1));
        path.push_back(spine[size_t(anchor)]);
        Vec3 cursor = path.front().position;
        Vec3 direction{unit(rng) * 0.4f, unit(rng) * (style == "vertical" ? 0.5f : 0.22f),
                       (unit(rng) < 0.f ? -1.f : 1.f) * (0.15f + positive(rng) * 0.2f)};
        for (int step = 0; step < points; ++step) {
            direction.x = std::clamp(direction.x + unit(rng) * 0.13f, -0.35f, 0.35f);
            direction.y = std::clamp(direction.y + unit(rng) * 0.1f, -0.32f, 0.32f);
            direction.z = std::clamp(direction.z + unit(rng) * 0.13f, -0.4f, 0.4f);
            cursor      = add(cursor, direction);
            cursor.x    = std::clamp(cursor.x, -0.76f, 0.76f);
            cursor.y    = std::clamp(cursor.y, -0.72f, 0.72f);
            cursor.z    = std::clamp(cursor.z, -0.74f, 0.74f);
            path.push_back({cursor, tunnelRadius * (0.58f + positive(rng) * 0.45f)});
        }
        branches.push_back({anchor, std::move(path)});
    }

    hydrology   = buildCaveHydrology(spine, branches, hydraulicErosion, hydraulicGradient, recharge, flowFocusing,
                                     damkohler, transportG);
    mixingSites = createCaveMixingSites(spine, branches, params.getSeed());
    constrictionScourSites = createCaveConstrictionScourSites(spine, branches, hydrology);
    knickpointSites        = createCaveKnickpointSites(spine, hydrology.trunk, sedimentLoad);


    chambers.reserve(size_t(chamberCount));
    primaryChamber               = chamberCount / 2;
    primaryChamberVerticalRadius = 0.f;
    for (int i = 0; i < chamberCount; ++i) {
        const float evenT  = float(i) / float(std::max(1, chamberCount - 1));
        const float jitter = chamberHierarchy > 0.f && i > 0 && i + 1 < chamberCount
                                 ? unit(rng) * chamberHierarchy * 0.32f / float(std::max(1, chamberCount - 1))
                                 : 0.f;
        const float anchoredT =
            i == primaryChamber && chamberHierarchy > 0.f ? 0.52f : std::clamp(evenT + jitter, 0.f, 1.f);
        const PassagePoint& anchor     = spine[size_t(std::lround(anchoredT * float(spinePoints - 1)))];
        const float         styleScale = style == "cavern" ? 1.35f : (style == "tunnels" ? 0.72f : 1.f);
        const bool          primary    = i == primaryChamber;
        const float         hierarchyScale =
            chamberHierarchy <= 0.f
                        ? 1.f
                        : (primary ? 1.f + 0.95f * chamberHierarchy : 1.f - chamberHierarchy * (0.12f + positive(rng) * 0.20f));
        const float radius = tunnelRadius * chamberScale * styleScale * (1.5f + positive(rng) * 1.25f) * hierarchyScale;
        const float verticalScale = primary ? 1.f + chamberHierarchy * 0.58f : 1.f;
        chambers.push_back({add(anchor.position, {unit(rng) * 0.08f, unit(rng) * 0.08f, unit(rng) * 0.08f}),
                            {std::min(0.82f, radius * (1.1f + positive(rng) * 0.8f)),
                             std::min(0.84f, radius * (0.75f + positive(rng) * 0.65f) * verticalScale),
                             std::min(0.82f, radius * (1.0f + positive(rng) * 0.9f))},
                            chamberIrregularity * (primary ? 1.f : 0.65f),
                            positive(rng) * 6.283185307f});
        if (primary) primaryChamberVerticalRadius = chambers.back().radii.y;
    }

    breakdownChambers.reserve(chambers.size());
    for (const Chamber& chamber : chambers)
        breakdownChambers.push_back(
            {chamber.center.x, chamber.center.y, chamber.center.z, chamber.radii.x, chamber.radii.y, chamber.radii.z});
    breakdownSet = createCaveBreakdown(breakdownChambers, breakdownEvents, breakdown, params.getSeed());
    obstacleScourSites =
        createCaveObstacleScourSites(breakdownSet, spine, hydrology.trunk, sedimentLoad, multiscaleRoughness);

    sedimentPath.reserve(spine.size());
    for (const PassagePoint& point : spine)
        sedimentPath.push_back({point.position.x, point.position.y, point.position.z, point.radius});
    sedimentSet = createCaveSediment(sedimentPath, sedimentBars, sedimentDeposition, paragenesis, params.getSeed());

    // Natural carbonate caves preferentially enlarge pre-existing joints.  A sparse set of
    // deterministic vertical fracture planes approximates that structural control without
    // requiring a full reactive-transport solve at recipe-build time.

    fractures.reserve(size_t(fractureCount));
    for (int i = 0; i < fractureCount; ++i) {
        const float angle = positive(rng) * 3.1415926535f;
        fractures.push_back({std::cos(angle), std::sin(angle), unit(rng) * 0.58f, 0.012f + positive(rng) * 0.025f});
    }
    potholeSites = createCavePotholeSites(spine, hydrology.trunk, fractures, fractureApertureVariability,
                                          fractureStressControl, sedimentLoad, potholeGravelSize, params.getSeed());


    if (genesis != "epigene") {
        cupolas.reserve(size_t(cupolaCount));
        feeders.reserve(size_t(feederCount));
        ceilingChannels.reserve(size_t(feederCount));
        for (int i = 0; i < cupolaCount; ++i) {
            const Chamber& host = chambers[size_t(rng() % uint32_t(chambers.size()))];
            const float    rx   = 0.045f + positive(rng) * 0.065f;
            const float    rz   = 0.045f + positive(rng) * 0.065f;
            const float    ry   = 0.08f + positive(rng) * 0.15f;
            cupolas.push_back(
                {{host.center.x + unit(rng) * host.radii.x * 0.45f, host.center.y + host.radii.y * 0.82f + ry * 0.45f,
                  host.center.z + unit(rng) * host.radii.z * 0.45f},
                 {rx, ry, rz}});
        }
        for (int i = 0; i < feederCount; ++i) {
            const Chamber& host = chambers[size_t(rng() % uint32_t(chambers.size()))];
            const Vec3     top{host.center.x + unit(rng) * host.radii.x * 0.32f, host.center.y - host.radii.y * 0.35f,
                           host.center.z + unit(rng) * host.radii.z * 0.32f};
            const Vec3     bottom{top.x + unit(rng) * 0.06f, std::max(-0.96f, top.y - (0.20f + positive(rng) * 0.35f)),
                              top.z + unit(rng) * 0.06f};
            const float    radius = 0.035f + positive(rng) * 0.035f;
            feeders.push_back({bottom, top, radius});
            if (!cupolas.empty()) {
                const Chamber& outlet = cupolas[size_t(i) % cupolas.size()];
                const Vec3     ceilingStart{top.x, host.center.y + host.radii.y * 0.72f, top.z};
                ceilingChannels.push_back({ceilingStart, outlet.center, radius * 0.62f});
            }
        }
    }

    // Each drip site creates a slender ceiling deposit and a broader floor deposit.  The
    // latter samples the conical, columnar and flat-topped steady forms described by the
    // reaction/advection (Damkohler) family, rather than mirroring the stalactite.

    dripstones.reserve(size_t(dripstoneCount) * 2u);
    columnCount = 0;
    for (int i = 0; i < dripstoneCount; ++i) {
        const Chamber& chamber    = chambers[size_t(rng() % uint32_t(chambers.size()))];
        const float    dripAngle  = positive(rng) * 2.f * 3.1415926535f;
        const float    dripRadius = 0.34f + positive(rng) * 0.30f;
        const float    dx         = std::cos(dripAngle) * chamber.radii.x * dripRadius;
        const float    dz         = std::sin(dripAngle) * chamber.radii.z * dripRadius;
        const float    radial     = std::min(
            0.86f, (dx * dx) / (chamber.radii.x * chamber.radii.x) + (dz * dz) / (chamber.radii.z * chamber.radii.z));
        const float yExtent = chamber.radii.y * std::sqrt(1.f - radial);
        const Vec3  ceiling{chamber.center.x + dx, chamber.center.y + yExtent, chamber.center.z + dz};
        const Vec3  floor{ceiling.x, chamber.center.y - yExtent, ceiling.z};
        const float gap           = std::max(0.08f, ceiling.y - floor.y);
        const bool  column        = positive(rng) < 0.12f * dripstoneScale;
        const float ceilingLength = gap * (column ? 0.54f : (0.18f + positive(rng) * 0.28f)) * dripstoneScale;
        const float floorLength   = gap * (column ? 0.54f : (0.14f + positive(rng) * 0.23f)) * dripstoneScale;
        const float ceilingRadius = std::clamp(ceilingLength * (0.14f + positive(rng) * 0.08f), 0.025f, 0.075f);
        dripstones.push_back({add(ceiling, {0.f, 0.018f, 0.f}), add(ceiling, {0.f, -ceilingLength, 0.f}), ceilingRadius,
                              column ? ceilingRadius * 0.72f : ceilingRadius * 0.13f});

        std::string shape = stalagmiteShape;
        if (shape == "mixed") {
            const float selector = positive(rng);
            shape                = selector < 0.42f ? "conical" : (selector < 0.78f ? "columnar" : "flatTop");
        }
        const float floorRadius = std::clamp(floorLength * (shape == "conical"    ? 0.38f
                                                            : shape == "columnar" ? 0.28f
                                                                                  : 0.45f),
                                             0.035f, 0.12f);
        const float tipRatio    = column ? 0.72f : (shape == "conical" ? 0.12f : shape == "columnar" ? 0.68f : 0.88f);
        dripstones.push_back({add(floor, {0.f, -0.018f, 0.f}), add(floor, {0.f, floorLength, 0.f}), floorRadius,
                              floorRadius * tipRatio});
        if (column) ++columnCount;
    }


    flowstones.reserve(size_t(flowstoneCount));
    for (int i = 0; i < flowstoneCount; ++i) {
        const Chamber& chamber = chambers[size_t(rng() % uint32_t(chambers.size()))];
        const float    angle   = positive(rng) * 2.f * 3.1415926535f;
        const Vec3     normal{std::cos(angle), 0.f, std::sin(angle)};
        const Vec3     tangent{-normal.z, 0.f, normal.x};
        // Keep the deposit centred on the parent chamber wall.  Moving it farther
        // inward creates a detached calcite lens after voxelisation.
        const Vec3 wall{chamber.center.x + normal.x * chamber.radii.x * 0.98f,
                        chamber.center.y + unit(rng) * chamber.radii.y * 0.22f,
                        chamber.center.z + normal.z * chamber.radii.z * 0.98f};
        flowstones.push_back({wall, normal, tangent, (0.055f + positive(rng) * 0.065f) * flowstoneScale,
                              (0.09f + positive(rng) * 0.13f) * flowstoneScale,
                              (0.026f + positive(rng) * 0.018f) * flowstoneScale,
                              (0.005f + positive(rng) * 0.009f) * flowstoneScale, 4.f + positive(rng) * 4.f,
                              positive(rng) * 2.f * 3.1415926535f});
    }


    curtains.reserve(size_t(curtainCount));
    for (int i = 0; i < curtainCount; ++i) {
        const Chamber& chamber = chambers[size_t(rng() % uint32_t(chambers.size()))];
        const float    angle   = positive(rng) * 2.f * 3.1415926535f;
        const float    radial  = 0.38f + positive(rng) * 0.32f;
        const float    dx      = std::cos(angle) * chamber.radii.x * radial;
        const float    dz      = std::sin(angle) * chamber.radii.z * radial;
        const float    radial2 = std::min(
            0.9f, (dx * dx) / (chamber.radii.x * chamber.radii.x) + (dz * dz) / (chamber.radii.z * chamber.radii.z));
        const Vec3 anchor{chamber.center.x + dx, chamber.center.y + chamber.radii.y * std::sqrt(1.f - radial2) + 0.018f,
                          chamber.center.z + dz};
        const Vec3 normal{std::cos(angle), 0.f, std::sin(angle)};
        const Vec3 tangent{-normal.z, 0.f, normal.x};
        curtains.push_back({anchor, normal, tangent, (0.045f + positive(rng) * 0.07f) * flowstoneScale,
                            (0.10f + positive(rng) * 0.18f) * flowstoneScale,
                            (0.024f + positive(rng) * 0.016f) * flowstoneScale,
                            (0.008f + positive(rng) * 0.014f) * flowstoneScale, 2.5f + positive(rng) * 2.5f,
                            positive(rng) * 2.f * 3.1415926535f});
    }


    return Result<void>::success();
}

}  // namespace eve::procgen::cave_detail
