#include "ScriptTest.h"
#include "common/Profile.h"
#include "profiler/Profiler.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

using eve::profiler::Profiler;

TEST_CASE("Profiler capture and naming contract") {
    SUB_CASE("profiler contract: disabled core does not publish a frame") {
        eve::prof::Profiler::reset();
        eve::prof::Profiler::setEnabled(false);
        eve::prof::Profiler::zoneBegin("ignored");
        eve::prof::Profiler::zoneEnd();
        eve::prof::Profiler::frameMark();
        REQUIRE(!eve::prof::Profiler::hasFrame());
        eve::prof::Profiler::reset();
    };

    SUB_CASE("profiler contract: capture requires a completed frame and reset removes it") {
        auto* profiler = Profiler::create();
        profiler->setEnabled(false);
        profiler->reset();
        REQUIRE(!profiler->hasFrame());
        REQUIRE(!profiler->captureFrame().ok());
        profiler->setEnabled(true);
        profiler->beginFrame();
        REQUIRE(!profiler->captureFrame().ok());
        profiler->endFrame();
        auto frame = profiler->captureFrame();
        REQUIRE(frame.ok());
        REQUIRE_EQ(frame.value().sequence, 1u);
        REQUIRE(frame.value().cpuFrameMs >= 0);
        profiler->reset();
        REQUIRE(!profiler->captureFrame().ok());
        profiler->setEnabled(false);
    };
}

TEST_CASE("profiler contract: interned names survive temporary strings and repeated insertions") {
    auto*       profiler = Profiler::create();
    const char* first    = profiler->internScopeName(std::string("short-lived"));
    const char* second   = profiler->internScopeName(std::string("short-lived"));
    REQUIRE(first == second);
    REQUIRE_EQ(std::string(first), "short-lived");
    REQUIRE_EQ(std::string(profiler->internScopeName("")), "scope");
}

extern const char* profiler_content;
UnitSciptTest(ProfilerScriptTest, profiler_content);
