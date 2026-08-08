#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "devtools/RenderFlow.hpp"
#include "common/RenderTrace.h"
#include "common/Exception.h"

#include <string>
#include <vector>

using namespace eve::dev;

static bool pathHas(const RenderSliceResult& s, RenderEventKind kind, const char* nameOrDetail) {
    for (const auto& e : s.path) {
        if (e.kind != kind) continue;
        if (e.name == nameOrDetail || e.detail == nameOrDetail) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Core slicing
// ---------------------------------------------------------------------------

TEST_CASE("devtools.renderflow.passAndBindSlice") {
    RenderFlow flow;
    flow.frameBegin();
    flow.passBegin("RenderSystem2D");
    flow.target("screen");
    flow.bind("texture", "hero");
    flow.draw("drawTexturedRectShaderUV", "hero");
    flow.error("draw failed: bad descriptor");
    flow.passEnd("RenderSystem2D");
    flow.frameEnd();

    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(!slice.passes.empty());
    CHECK_EQ(slice.passes.back(), std::string("RenderSystem2D"));
    CHECK(pathHas(slice, RenderEventKind::Bind, "hero"));
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawTexturedRectShaderUV"));
    CHECK(pathHas(slice, RenderEventKind::Error, "error"));

    const std::string report = flow.formatErrorReport("draw failed: bad descriptor");
    CHECK(report.find("Render Pipeline Trace") != std::string::npos);
    CHECK(report.find("RenderSystem2D") != std::string::npos);
    CHECK(report.find("hero") != std::string::npos);
}

TEST_CASE("devtools.renderflow.nestedPasses") {
    RenderFlow flow;
    flow.frameBegin();
    flow.passBegin("RenderSystem3D");
    flow.passBegin("ShadowPass");
    flow.bind("mesh", "caster");
    flow.draw("drawMeshShadow", "cascade");
    flow.passEnd("ShadowPass");
    flow.bind("mesh", "rock");
    flow.bind("texture", "albedo");
    flow.draw("drawMeshShader", "default");
    flow.error("pipeline create failed");
    // still inside RenderSystem3D
    RenderSliceResult slice = flow.sliceBackward({});
    REQUIRE(slice.passes.size() >= 1u);
    CHECK_EQ(slice.passes[0], std::string("RenderSystem3D"));
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawMeshShader"));
    const bool sawMeshOrAlbedo = pathHas(slice, RenderEventKind::Bind, "rock") ||
                                 pathHas(slice, RenderEventKind::Bind, "albedo");
    CHECK(sawMeshOrAlbedo);
}

TEST_CASE("devtools.renderflow.resourceFocusedCriterion") {
    RenderFlow flow;
    flow.passBegin("RenderSystem2D");
    flow.bind("texture", "bg");
    flow.draw("drawTexturedRectShaderUV", "bg");
    flow.bind("texture", "hero");
    flow.draw("drawTexturedRectShaderUV", "hero");
    flow.error("hero atlas missing");
    RenderSliceCriterion c;
    c.resource = "hero";
    RenderSliceResult slice = flow.sliceBackward(c);
    CHECK(pathHas(slice, RenderEventKind::Bind, "hero"));
    CHECK(pathHas(slice, RenderEventKind::Error, "error"));
}

TEST_CASE("devtools.renderflow.eventIdSeed") {
    RenderFlow flow;
    flow.passBegin("P");
    flow.bind("shader", "s1");
    const uint32_t drawId = [&]() {
        flow.draw("drawSolidRect", "solid");
        return flow.events().back().id;
    }();
    flow.bind("shader", "s2");
    flow.draw("drawSolidRect", "solid2");
    flow.error("later");

    RenderSliceCriterion c;
    c.eventId = drawId;
    RenderSliceResult slice = flow.sliceBackward(c);
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawSolidRect"));
    // Seeded on first draw — should see s1 bind via data deps / parent chain.
    bool sawS1 = pathHas(slice, RenderEventKind::Bind, "s1");
    CHECK(sawS1);
}

TEST_CASE("devtools.renderflow.emptySlice") {
    RenderFlow flow;
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(slice.eventIds.empty());
    CHECK(slice.summary.find("empty") != std::string::npos);
}

TEST_CASE("devtools.renderflow.clear") {
    RenderFlow flow;
    flow.frameBegin();
    flow.draw("x", "y");
    flow.clear();
    CHECK_EQ(flow.eventCount(), 0u);
    CHECK(flow.event(1) == nullptr);
}

TEST_CASE("devtools.renderflow.drawWithoutBinds") {
    RenderFlow flow;
    flow.passBegin("RenderSystem2D");
    flow.draw("drawSolidRect", "solid");
    flow.error("clear color invalid");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::PassBegin, "RenderSystem2D"));
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawSolidRect"));
}

TEST_CASE("devtools.renderflow.errorWithoutDraw") {
    RenderFlow flow;
    flow.passBegin("Present");
    flow.error("swapchain recreate failed");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Error, "error"));
    CHECK(pathHas(slice, RenderEventKind::PassBegin, "Present"));
}

TEST_CASE("devtools.renderflow.multipleErrorsSeedsLast") {
    RenderFlow flow;
    flow.passBegin("A");
    flow.error("first");
    flow.passEnd("A");
    flow.passBegin("B");
    flow.bind("mesh", "m");
    flow.error("second");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::PassBegin, "B"));
    CHECK(pathHas(slice, RenderEventKind::Bind, "m"));
    // Active passes at last error should be B only.
    REQUIRE(!slice.passes.empty());
    CHECK_EQ(slice.passes.back(), std::string("B"));
}

TEST_CASE("devtools.renderflow.targetSwitch") {
    RenderFlow flow;
    flow.passBegin("RenderSystem2D");
    flow.target("screen");
    flow.draw("drawSolidRect", "a");
    flow.target("canvas");
    flow.draw("drawSolidRect", "b");
    flow.error("canvas size 0");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Target, "canvas"));
}

TEST_CASE("devtools.renderflow.bindKinds") {
    RenderFlow flow;
    flow.passBegin("RenderSystem3D");
    flow.bind("texture", "albedo");
    flow.bind("shader", "pbr");
    flow.bind("mesh", "rock");
    flow.bind("font", "ui");
    flow.bind("canvas", "offscreen");
    flow.draw("drawMeshShader", "pbr");
    flow.error("descriptor");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Bind, "albedo"));
    const bool sawShaderOrDraw = pathHas(slice, RenderEventKind::Bind, "pbr") ||
                                 pathHas(slice, RenderEventKind::Draw, "drawMeshShader");
    CHECK(sawShaderOrDraw);
    CHECK(pathHas(slice, RenderEventKind::Bind, "rock"));
}

TEST_CASE("devtools.renderflow.twoFramesErrorInSecond") {
    RenderFlow flow;
    flow.frameBegin();
    flow.passBegin("RenderSystem2D");
    flow.draw("drawSolidRect", "ok");
    flow.passEnd("RenderSystem2D");
    flow.frameEnd();

    flow.frameBegin();
    flow.passBegin("RenderSystem2D");
    flow.bind("texture", "bad");
    flow.draw("drawTexturedRectShaderUV", "bad");
    flow.error("oom");
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Bind, "bad"));
    CHECK(pathHas(slice, RenderEventKind::Error, "error"));
}

TEST_CASE("devtools.renderflow.formatReportTruncates") {
    RenderFlow flow;
    flow.passBegin("P");
    for (int i = 0; i < 80; ++i) {
        flow.bind("texture", ("t" + std::to_string(i)).c_str());
        flow.draw("draw", ("d" + std::to_string(i)).c_str());
    }
    flow.error("boom");
    const std::string report = flow.formatErrorReport("boom");
    CHECK(report.find("...") != std::string::npos);
    CHECK(report.find("render slice:") != std::string::npos);
}

TEST_CASE("devtools.renderflow.passEndPopsStack") {
    RenderFlow flow;
    flow.passBegin("Outer");
    flow.passBegin("Inner");
    flow.passEnd("Inner");
    flow.error("after inner");
    RenderSliceResult slice = flow.sliceBackward({});
    REQUIRE(slice.passes.size() == 1u);
    CHECK_EQ(slice.passes[0], std::string("Outer"));
}

TEST_CASE("devtools.renderflow.nullNameArgs") {
    RenderFlow flow;
    flow.passBegin(nullptr);
    flow.bind(nullptr, nullptr);
    flow.draw(nullptr, nullptr);
    flow.target(nullptr);
    flow.error(nullptr);
    CHECK(flow.eventCount() >= 5u);
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(!slice.eventIds.empty());
}

// ---------------------------------------------------------------------------
// Ring buffer
// ---------------------------------------------------------------------------

TEST_CASE("devtools.renderflow.ringBuffer") {
    RenderFlow flow;
    flow.setMaxEvents(16);
    for (int i = 0; i < 40; ++i) flow.draw("drawSolidRect", "solid");
    CHECK_EQ(flow.eventCount(), 16u);
    CHECK(flow.event(flow.events().front().id - 1) == nullptr);
}

TEST_CASE("devtools.renderflow.setMaxEventsClampShrink") {
    RenderFlow flow;
    flow.setMaxEvents(1);
    CHECK_EQ(flow.maxEvents(), 16u);
    for (int i = 0; i < 20; ++i) flow.draw("d", "x");
    CHECK_EQ(flow.eventCount(), 16u);
    flow.setMaxEvents(16);
    CHECK_EQ(flow.eventCount(), 16u);
}

TEST_CASE("devtools.renderflow.ringKeepsRecentErrorContext") {
    RenderFlow flow;
    flow.setMaxEvents(16);
    for (int i = 0; i < 40; ++i) {
        flow.passBegin("Old");
        flow.draw("old", "x");
        flow.passEnd("Old");
    }
    flow.passBegin("Hot");
    flow.bind("mesh", "last");
    flow.draw("drawMeshShader", "last");
    flow.error("fail");
    CHECK_EQ(flow.eventCount(), 16u);
    RenderSliceResult slice = flow.sliceBackward({});
    const bool sawHotOrError = pathHas(slice, RenderEventKind::PassBegin, "Hot") ||
                               pathHas(slice, RenderEventKind::Error, "error");
    CHECK(sawHotOrError);
    const bool sawLast = pathHas(slice, RenderEventKind::Bind, "last") ||
                         pathHas(slice, RenderEventKind::Draw, "drawMeshShader");
    CHECK(sawLast);
}

TEST_CASE("devtools.renderflow.oneInOneOut") {
    RenderFlow flow;
    flow.setMaxEvents(16);
    for (int i = 0; i < 16; ++i) flow.draw("d", "x");
    const uint32_t front = flow.events().front().id;
    flow.draw("d", "new");
    CHECK_EQ(flow.eventCount(), 16u);
    CHECK_EQ(flow.events().front().id, front + 1);
    CHECK_EQ(flow.events().back().detail, std::string("new"));
}

// ---------------------------------------------------------------------------
// Hooks / Exception / RAII
// ---------------------------------------------------------------------------

TEST_CASE("devtools.renderflow.hookNoopWhenUnset") {
    eve::debug::setRenderTracer(nullptr);
    eve::debug::rtFrameBegin();
    eve::debug::rtPassBegin("X");
    eve::debug::rtBind("texture", "t");
    eve::debug::rtDraw("draw", "d");
    eve::debug::rtPassEnd("X");
    eve::debug::rtFrameEnd();
    eve::debug::rtError("no tracer");
    CHECK(eve::debug::renderTracer() == nullptr);
}

TEST_CASE("devtools.renderflow.hookViaException") {
    RenderFlow flow;
    eve::debug::setRenderTracer(&flow);
    eve::debug::rtFrameBegin();
    eve::debug::rtPassBegin("RenderSystem3D");
    eve::debug::rtBind("mesh", "rock");
    eve::debug::rtDraw("drawMeshShader", "default");
    try {
        throw eve::Exception("failed to create mesh3d-style pipeline");
    } catch (const eve::Exception& e) {
        const std::string report = flow.formatErrorReport(e.what());
        CHECK(report.find("Render Pipeline Trace") != std::string::npos);
        CHECK(report.find("RenderSystem3D") != std::string::npos);
        const bool hasDrawOrBind = report.find("drawMeshShader") != std::string::npos ||
                                   report.find("rock") != std::string::npos;
        CHECK(hasDrawOrBind);
        CHECK(std::string(e.what()).find("mesh3d") != std::string::npos);
    }
    eve::debug::setRenderTracer(nullptr);
    CHECK(eve::debug::renderTracer() == nullptr);
}

TEST_CASE("devtools.renderflow.exceptionWithoutTracer") {
    eve::debug::setRenderTracer(nullptr);
    try {
        throw eve::Exception("newTexture: invalid args");
    } catch (const eve::Exception& e) {
        CHECK(std::string(e.what()).find("newTexture") != std::string::npos);
    }
}

TEST_CASE("devtools.renderflow.passScopeRAII") {
    RenderFlow flow;
    eve::debug::setRenderTracer(&flow);
    {
        eve::debug::RenderPassScope scope("ScopedPass");
        eve::debug::rtDraw("inside", "x");
    }
    // After scope, pass should be ended.
    bool sawBegin = false, sawEnd = false;
    for (const auto& e : flow.events()) {
        if (e.kind == RenderEventKind::PassBegin && e.name == "ScopedPass") sawBegin = true;
        if (e.kind == RenderEventKind::PassEnd && e.name == "ScopedPass") sawEnd = true;
    }
    CHECK(sawBegin);
    CHECK(sawEnd);
    eve::debug::setRenderTracer(nullptr);
}

TEST_CASE("devtools.renderflow.rtHelpersForward") {
    RenderFlow flow;
    eve::debug::setRenderTracer(&flow);
    eve::debug::rtFrameBegin();
    eve::debug::rtTarget("screen");
    eve::debug::rtBind("font", "main");
    eve::debug::rtDraw("print", "text");
    eve::debug::rtError("Graphics::print: no font set");
    eve::debug::rtFrameEnd();
    CHECK(flow.eventCount() >= 6u);
    RenderSliceResult slice = flow.sliceBackward({});
    const bool sawPrint = pathHas(slice, RenderEventKind::Bind, "main") ||
                          pathHas(slice, RenderEventKind::Draw, "print");
    CHECK(sawPrint);
    eve::debug::setRenderTracer(nullptr);
}

TEST_CASE("devtools.renderflow.shadowThenMainPass") {
    RenderFlow flow;
    flow.frameBegin();
    flow.passBegin("RenderSystem3D");
    for (int c = 0; c < 3; ++c) {
        flow.passBegin("ShadowPass");
        flow.bind("mesh", "caster");
        flow.draw("drawMeshShadow", "cascade");
        flow.passEnd("ShadowPass");
    }
    flow.target("screen");
    flow.bind("mesh", "hero");
    flow.bind("texture", "heroAlbedo");
    flow.draw("drawMeshShader", "default");
    flow.error("validation: unbound descriptor");
    flow.passEnd("RenderSystem3D");
    flow.frameEnd();

    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawMeshShader"));
    CHECK(pathHas(slice, RenderEventKind::PassBegin, "RenderSystem3D"));
    const std::string report = flow.formatErrorReport("validation: unbound descriptor");
    CHECK(report.find("Active passes") != std::string::npos);
}

TEST_CASE("devtools.renderflow.simulateSpriteBatch") {
    RenderFlow flow;
    flow.setMaxEvents(128);
    flow.frameBegin();
    flow.passBegin("RenderSystem2D");
    flow.target("screen");
    for (int i = 0; i < 50; ++i) {
        flow.bind("texture", "sprite");
        flow.draw("drawTexturedRectShaderUV", "textured");
    }
    flow.bind("texture", "boss");
    flow.draw("drawTexturedRectLitUV", "lit2d");
    flow.error("lit2d normal missing");
    flow.passEnd("RenderSystem2D");
    flow.passBegin("Present");
    flow.passEnd("Present");
    flow.frameEnd();

    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Draw, "drawTexturedRectLitUV"));
    CHECK(pathHas(slice, RenderEventKind::Error, "error"));
}

TEST_CASE("devtools.renderflow.rebindSameNameUpdatesReaching") {
    RenderFlow flow;
    flow.passBegin("P");
    flow.bind("texture", "hero");  // v1
    flow.draw("draw", "hero");
    flow.bind("texture", "hero");  // v2 overwrite
    flow.draw("draw", "hero");
    flow.error("bad");
    // Last draw should prefer the second bind; both may appear via control chain,
    // but data deps for the last draw should include a Bind of hero.
    RenderSliceResult slice = flow.sliceBackward({});
    CHECK(pathHas(slice, RenderEventKind::Bind, "hero"));
}

TEST_CASE("devtools.renderflow.eventLookupContiguous") {
    RenderFlow flow;
    flow.setMaxEvents(16);
    for (int i = 0; i < 20; ++i) flow.draw("d", "x");
    const uint32_t lo = flow.events().front().id;
    const uint32_t hi = flow.events().back().id;
    for (uint32_t id = lo; id <= hi; ++id) {
        REQUIRE(flow.event(id) != nullptr);
        CHECK_EQ(flow.event(id)->id, id);
    }
    CHECK(flow.event(lo - 1) == nullptr);
    CHECK(flow.event(hi + 1) == nullptr);
}
