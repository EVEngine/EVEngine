#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "animation/AnimBatch.h"
#include "animation/AnimClip.h"
#include "animation/AnimPose.h"
#include "animation/AnimSkeleton.h"

#include <cmath>
#include <memory>
#include <vector>

using namespace eve::animation;


TEST_CASE("animation.pose.simdBlendMatchesTrsBlend") {
    AnimPose a(1), b(1), out;
    a.setLocalPosition(0, 0.f, 2.f, 4.f);
    a.setLocalScale(0, 1.f, 2.f, 3.f);
    b.setLocalPosition(0, 10.f, 6.f, 0.f);
    b.setLocalScale(0, 3.f, 4.f, 5.f);
    out.blendFrom(&a, &b, 0.25f);
    CHECK(std::fabs(out.getLocalPositionX(0) - 2.5f) < 1e-6f);
    CHECK(std::fabs(out.getLocalPositionY(0) - 3.f) < 1e-6f);
    CHECK(std::fabs(out.getLocalScaleZ(0) - 3.5f) < 1e-6f);
}
