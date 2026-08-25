#include "sensing/Sensing.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::sensing;
TEST_CASE("sensing.filtersAndSortsCandidates") {
    SensingWorld w;
    REQUIRE(w.upsert("b", 3, 4, "red", "unit,tank", "p1"));
    REQUIRE(w.upsert("a", 1, 0, "blue", "unit,infantry", "p1,p2"));
    REQUIRE(w.upsert("c", 2, 0, "red", "building", "p1"));
    CHECK_EQ(w.circle(0, 0, 10, "unit", "", "", "blue", "p1", 8), 1);
    REQUIRE(w.resultAt(0));
    CHECK_EQ(w.resultAt(0)->id, std::string("b"));
}
TEST_CASE("sensing.ordersByDistanceThenIdAndLimits") {
    SensingWorld w;
    w.upsert("z", 1, 0, "", "u", "");
    w.upsert("a", -1, 0, "", "u", "");
    CHECK_EQ(w.circle(0, 0, 2, "u", "", "", "", "", 1), 1);
    CHECK_EQ(w.resultAt(0)->id, std::string("a"));
}
TEST_CASE("sensing.snapshotIsTransactional") {
    SensingWorld w;
    w.upsert("u", 2, 3, "f", "b,a", "p");
    auto         s = w.snapshotJson();
    SensingWorld x;
    REQUIRE(x.restoreJson(s));
    CHECK_EQ(x.snapshotJson(), s);
    auto before = x.snapshotJson();
    CHECK(!x.restoreJson("{}"));
    CHECK_EQ(x.snapshotJson(), before);
}
