#include "procgen/PcgSpawnProgress.h"
#include <zeroerr/unittest.h>
#include <cmath>
using namespace eve::procgen;
TEST_CASE("Pcg spawn progress combines completed rules and current fraction") {
    PcgSpawnProgress progress;
    auto started=progress.updateRule("Forest",10,3,4,1); REQUIRE(started.ok());
    CHECK(progress.title()=="Spawning"); CHECK(progress.subtitle()=="Forest 1 of 4 Rules");
    REQUIRE(progress.updateRuleFraction(0.5).ok()); CHECK(std::abs(progress.progress()-0.35)<1e-12);
}
TEST_CASE("Pcg spawn progress validates atomically and exposes cancellation") {
    PcgSpawnProgress progress; REQUIRE(progress.updateRule("Biome",2,0,2,0).ok());
    CHECK(!progress.updateRuleFraction(2).ok()); CHECK(progress.progress()==0);
    auto cancelled=progress.requestCancel(); REQUIRE(cancelled.ok());
    CHECK(progress.status()==PcgSpawnProgressStatus::CancelRequested);
    CHECK(progress.clear()==PcgSpawnProgressStatus::Hidden); CHECK(progress.title().empty());
}
TEST_CASE("Pcg spawn progress completes on the final rule") {
    PcgSpawnProgress progress; auto result=progress.updateRule("World",5,5,1,1); REQUIRE(result.ok());
    CHECK(progress.status()==PcgSpawnProgressStatus::Completed); CHECK(progress.progress()==1);
    CHECK(!progress.requestCancel().ok());
}
