#include "profiler/Profiler.h"
#include "zeroerr/unittest.h"

#include <string>

#if !defined(EVE_TEST_FILTER_ORIGINS) || defined(EVE_TEST_ORIGIN_profiler)
namespace {
bool containsZone(const eve::profiler::ProfilerFrameSnapshot& frame, const std::string& name) {
    for (const auto& zone : frame.zones)
        if (zone.name == name) return true;
    return false;
}
}  // namespace

TEST_CASE("profiler contract: manual and render-pass scopes appear in owning snapshots") {
    auto* profiler = eve::profiler::Profiler::create();
    profiler->reset();
    profiler->setEnabled(true);
    profiler->beginFrame();
    profiler->begin("manualScope");
    profiler->end();
    profiler->passBegin("ShadowPass");
    profiler->passEnd("ShadowPass");
    profiler->endFrame();
    auto first = profiler->captureFrame();
    REQUIRE(first.ok());
    REQUIRE(containsZone(first.value(), "manualScope"));
    REQUIRE(containsZone(first.value(), "ShadowPass"));
    REQUIRE(profiler->textReport().find("ShadowPass") != std::string::npos);
    profiler->beginFrame();
    profiler->begin("nextFrame");
    profiler->end();
    profiler->endFrame();
    auto second = profiler->captureFrame();
    REQUIRE(second.ok());
    REQUIRE_EQ(second.value().sequence, 2u);
    REQUIRE(containsZone(first.value(), "manualScope"));
    profiler->setEnabled(false);
    profiler->reset();
}
#endif
