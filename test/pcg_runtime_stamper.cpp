#include "procgen/heightmap/PcgRuntimeStamper.h"

#include <zeroerr/unittest.h>

#include <algorithm>
#include <cmath>

using namespace eve::procgen;

TEST_CASE("Pcg runtime stamper reproduces the sample orchestration atomically") {
    PcgRuntimeStamper stamper;
    REQUIRE(stamper.configure("Pcg\\Stamps\\Rugged", true, true).ok());
    CHECK(stamper.stampAddress() == "Pcg/Stamps/Rugged");
    CHECK(stamper.status() == PcgRuntimeStampStatus::AwaitingResource);

    Heightmap stamp(3, 3);
    std::fill(stamp.data().begin(), stamp.data().end(), 1.0F);
    REQUIRE(stamper.loadStamp(stamp, "Rugged").ok());
    Heightmap terrain(5, 5);
    std::fill(terrain.data().begin(), terrain.data().end(), 20.0F);
    auto result = stamper.execute(terrain);
    REQUIRE(result.ok());
    CHECK(terrain.height(2, 2) == 6.0F);
    CHECK(terrain.height(0, 0) == 0.0F);
    CHECK(stamper.status() == PcgRuntimeStampStatus::Completed);
    CHECK(stamper.progressText() == "Stamp progress: 1");
    CHECK(std::abs(stamper.updateTimeAllowed() - 1.0 / 15.0) < 1e-12);
}

TEST_CASE("Pcg runtime stamper exposes load failure and bottom-centred layout") {
    PcgRuntimeStamper stamper;
    REQUIRE(stamper.configure("missing\\stamp", false, true).ok());
    CHECK(!stamper.reportMissingStamp().ok());
    CHECK(stamper.progressText() == "Failed to load stamp at missing/stamp");
    CHECK(stamper.status() == PcgRuntimeStampStatus::Failed);
    REQUIRE(stamper.updateLayout(1920, 1080).ok());
    CHECK(stamper.labelCenterX() == 960.0);
    CHECK(stamper.labelCenterY() == 1060.0);
}

TEST_CASE("Pcg runtime stamper rejects invalid input without changing terrain") {
    PcgRuntimeStamper stamper;
    Heightmap terrain(2, 2);
    std::fill(terrain.data().begin(), terrain.data().end(), 3.0F);
    const auto before = terrain.data();
    CHECK(!stamper.execute(terrain).ok());
    CHECK(terrain.data() == before);
    CHECK(!stamper.configure("", true, true).ok());
}
