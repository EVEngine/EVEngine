#include "stylize/MeshVfxScalability.h"

#include <array>

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::stylize;

TEST_CASE("stylize.mesh_vfx_scalability selects deterministic tiers within budget") {
    MeshVfxScalabilityPolicy policy;
    policy.workUnitBudget = 7;
    MeshVfxScalabilityPlanner planner(policy);

    const std::array candidates{
        MeshVfxLodCandidate{30, 8.0f, 120.0f, 0, true},
        MeshVfxLodCandidate{10, 4.0f, 120.0f, 10, true},
        MeshVfxLodCandidate{20, 6.0f, 60.0f, 5, true},
        MeshVfxLodCandidate{40, 10.0f, 20.0f, -1, true},
    };

    const auto decisions = planner.plan(candidates);
    REQUIRE_EQ(decisions.size(), candidates.size());
    REQUIRE_EQ(static_cast<int>(decisions[0].tier), static_cast<int>(MeshVfxLodTier::Culled));
    REQUIRE_EQ(static_cast<int>(decisions[1].tier), static_cast<int>(MeshVfxLodTier::Full));
    REQUIRE_EQ(static_cast<int>(decisions[2].tier), static_cast<int>(MeshVfxLodTier::Reduced));
    REQUIRE_EQ(static_cast<int>(decisions[3].tier), static_cast<int>(MeshVfxLodTier::Minimal));
    REQUIRE_EQ(decisions[2].trailSampleStride, 2u);
    REQUIRE_EQ(decisions[3].meshUpdateInterval, 4u);
}

TEST_CASE("stylize.mesh_vfx_scalability is independent of candidate iteration order") {
    MeshVfxScalabilityPolicy policy;
    policy.workUnitBudget = 4;
    MeshVfxScalabilityPlanner planner(policy);

    const std::array forward{
        MeshVfxLodCandidate{2, 5.0f, 100.0f, 0, true},
        MeshVfxLodCandidate{1, 5.0f, 100.0f, 0, true},
    };
    const std::array reverse{forward[1], forward[0]};
    const auto a = planner.plan(forward);
    const auto b = planner.plan(reverse);

    REQUIRE_EQ(static_cast<int>(a[0].tier), static_cast<int>(MeshVfxLodTier::Culled));
    REQUIRE_EQ(static_cast<int>(a[1].tier), static_cast<int>(MeshVfxLodTier::Full));
    REQUIRE_EQ(static_cast<int>(b[0].tier), static_cast<int>(MeshVfxLodTier::Full));
    REQUIRE_EQ(static_cast<int>(b[1].tier), static_cast<int>(MeshVfxLodTier::Culled));
}

TEST_CASE("stylize.mesh_vfx_scalability culls invalid and out-of-range candidates") {
    MeshVfxScalabilityPlanner planner;
    const std::array candidates{
        MeshVfxLodCandidate{1, 1.0f, 100.0f, 0, false},
        MeshVfxLodCandidate{2, 81.0f, 100.0f, 0, true},
        MeshVfxLodCandidate{3, -1.0f, 100.0f, 0, true},
    };
    const auto decisions = planner.plan(candidates);
    for (const auto& decision : decisions) {
        REQUIRE_EQ(static_cast<int>(decision.tier), static_cast<int>(MeshVfxLodTier::Culled));
        REQUIRE_EQ(decision.workUnits, 0u);
    }
}
