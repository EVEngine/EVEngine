#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/FrameStatsPanel.hpp"

#include <cmath>
#include <limits>
#include <string>

using namespace eve::dev;

TEST_CASE("devtools.frameStats.sampleAndFormat") {
    auto& stats = FrameStatsPanel::instance();
    stats.setVisible(false);
    stats.clearEntityCount();
    CHECK(!stats.isVisible());
    stats.toggleVisible();
    CHECK(stats.isVisible());
    stats.setVisible(false);

    stats.sample(0.016f);
    CHECK(stats.fps() > 50.f);
    CHECK(stats.fps() < 70.f);
    CHECK(std::fabs(stats.frameMs() - 16.f) < 1.5f);
    CHECK(!stats.hasEntityCount());
    CHECK(stats.entityCount() < 0);

    stats.setEntityCount(42);
    CHECK(stats.hasEntityCount());
    CHECK(stats.entityCount() == 42);
    const std::string line = stats.format();
    CHECK(line.find("fps") != std::string::npos);
    CHECK(line.find("ms") != std::string::npos);
    CHECK(line.find("42") != std::string::npos);

    stats.clearEntityCount();
    CHECK(!stats.hasEntityCount());

    // Reject non-finite / non-positive samples without changing last good frameMs.
    const float goodMs = stats.frameMs();
    stats.sample(0.f);
    stats.sample(-1.f);
    stats.sample(std::numeric_limits<float>::quiet_NaN());
    CHECK(std::fabs(stats.frameMs() - goodMs) < 0.01f);
}
