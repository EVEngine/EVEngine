// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "profiler.cpp"
  ]
}
*/
// clang-format on

#include "zeroerr/assert.h"

#include <string>
#include "ScriptTest.h"
#include "common/Profile.h"
#include "profiler/Profiler.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// profiler.cpp
using eve::profiler::Profiler;

TEST_CASE("profiler.core.disabled.isNoop") {
    eve::prof::Profiler::reset();
    eve::prof::Profiler::setEnabled(false);
    eve::prof::Profiler::zoneBegin("a");
    eve::prof::Profiler::zoneEnd();
    eve::prof::Profiler::frameMark();
    CHECK_NOT(eve::prof::Profiler::hasFrame());
    eve::prof::Profiler::reset();
}


TEST_CASE("profiler.module.frameCapture") {
    auto* p = Profiler::create();
    p->reset();
    p->setEnabled(true);
    p->beginFrame();
    p->begin("moduleScope");
    p->end();
    p->endFrame();
    CHECK(p->hasFrame());
    CHECK(p->frameMs() >= 0.f);
    CHECK(p->textReport().find("moduleScope") != std::string::npos);
    p->setEnabled(false);
    p->reset();
}

TEST_CASE("profiler.module.renderPassZones") {
    auto* p = Profiler::create();
    p->reset();
    p->setEnabled(true);
    p->beginFrame();
    p->passBegin("ShadowPass");  // IRenderTracer pass -> graphics zone
    p->passEnd("ShadowPass");
    p->endFrame();
    CHECK(p->hasFrame());
    CHECK(p->textReport().find("ShadowPass") != std::string::npos);
    p->setEnabled(false);
    p->reset();
}

// profiler_script.cpp
extern const char* profiler_content;
UnitSciptTest(ProfilerScriptTest, profiler_content);
