#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Profile.h"
#include "profiler/Profiler.h"

#include <string>

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
    const auto& samples = eve::prof::Profiler::lastFrame();
    bool foundParent = false, foundChild = false;
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
