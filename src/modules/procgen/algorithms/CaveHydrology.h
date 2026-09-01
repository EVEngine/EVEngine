#pragma once

#include <string>
#include <vector>

namespace eve::procgen {

struct CaveHydrologyVec3 {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

struct CaveHydrologyPoint {
    CaveHydrologyVec3 position;
    float             radius = 0.2f;
};

struct CaveHydrologyBranch {
    int                             trunkAnchor = 0;
    std::vector<CaveHydrologyPoint> points;
};

struct CaveHydrologyWeights {
    std::vector<float>              trunk;
    std::vector<std::vector<float>> branches;
    float                           minimum             = 1.f;
    float                           maximum             = 1.f;
    float                           reactantPenetration = 1.f;
    std::string                     dissolutionRegime   = "disabled";
};

CaveHydrologyWeights buildCaveHydrology(const std::vector<CaveHydrologyPoint>&  trunk,
                                        const std::vector<CaveHydrologyBranch>& branches, float erosion, float gradient,
                                        float recharge, float focusing, float damkohler, float transportG);

}  // namespace eve::procgen
