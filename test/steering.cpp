#include "steering/Steering.h"
#include <cmath>
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::steering;
TEST_CASE("steering.seekFleeAndArrive") {
    auto s = Steering::seek(0, 0, 3, 4, 10);
    CHECK(std::abs(s.x - 6) < .001f);
    CHECK(std::abs(s.y - 8) < .001f);
    auto f = Steering::flee(0, 0, 3, 4, 5);
    CHECK(f.x < 0);
    CHECK(f.y < 0);
    CHECK_EQ(Steering::arrive(0, 0, .1f, 0, 5, 4, .5f).x, 0.f);
}
TEST_CASE("steering.pathSeparationAndAvoidance") {
    CHECK_EQ(Steering::pathTarget(0, 0, "0:0,5:0,10:0", 0, .2f), 1);
    auto s = Steering::separation(0, 0, "1:0,-3:0", 2, 4);
    CHECK(s.x < 0);
    auto a = Steering::avoid(0, 0, 1, 0, 2, 0, 1, 2, 3);
    CHECK(std::isfinite(a.x));
}
TEST_CASE("steering.invalidInputsReturnNeutral") {
    CHECK_EQ(Steering::seek(0, 0, 1, 1, -1).x, 0.f);
    CHECK_EQ(Steering::arrive(0, 0, 1, 1, 1, 0, 2).x, 0.f);
    CHECK_EQ(Steering::pathTarget(0, 0, "bad", 0, 1), -1);
}
