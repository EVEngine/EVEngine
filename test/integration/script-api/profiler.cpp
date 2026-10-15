#include "profiler/Profiler.h"
#include <string>
#include "ScriptTest.h"
#include "common/Profile.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// profiler.cpp
using eve::profiler::Profiler;


TEST_CASE("profiler.core.aggregatesSelfAndTotal") {
    eve::prof::Profiler::reset();
    eve::prof::Profiler::setEnabled(true);
    eve::prof::Profiler::zoneBegin("parent", "physics");
    eve::prof::Profiler::zoneBegin("child", "physics");
    eve::prof::Profiler::zoneEnd();
    eve::prof::Profiler::zoneEnd();
    const double frameMs = eve::prof::Profiler::frameMark();
    CHECK_GT(frameMs, 0.0);
    CHECK(eve::prof::Profiler::hasFrame());
    const auto& samples     = eve::prof::Profiler::lastFrame();
    bool        foundParent = false, foundChild = false;
    for (const auto& s : samples) {
        if (s.name == "parent" && s.module == "physics") {
            foundParent = true;
            // parent self time must be < its total time (a child ran inside it).
            CHECK_LT(s.selfMs, s.totalMs);
        }
        if (s.name == "child") foundChild = true;
    }
    CHECK(foundParent);
    CHECK(foundChild);
    CHECK(eve::prof::Profiler::textReport().find("parent") != std::string::npos);
    eve::prof::Profiler::setEnabled(false);
    eve::prof::Profiler::reset();
}


// profiler_script.cpp
extern const char* profiler_content;
UnitSciptTest(ProfilerScriptTest, profiler_content);

TEST_CASE_FIXTURE(ProfilerScriptTest, "ProfilerScriptTest.capture") {
    CHECK(vm.callFunc(vm.findFunc("captureWorks"), vm).toBool());
}
