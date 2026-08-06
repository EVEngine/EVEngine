#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "filesystem/Filesystem.h"
#include "filesystem/HotReload.h"
#include "particles/Particles.h"
#include "particles/ParticleEmitter.h"
#include "particles/ParticleSystem.h"

#include <cstring>
#include <memory>
#include <string>

namespace {

eve::filesystem::Filesystem *fs() { return eve::filesystem::Filesystem::create(); }

void useIdentity(const char *id) {
    auto *f = fs();
    REQUIRE(f->setIdentity(id, true));
    REQUIRE(f->setupWriteDirectory());
}

}  // namespace

TEST_CASE("hotreload.normalizePath") {
    CHECK_EQ(eve::filesystem::HotReload::normalizePath("./a/b.nut"), std::string("a/b.nut"));
    CHECK_EQ(eve::filesystem::HotReload::normalizePath("a\\b.json"), std::string("a/b.json"));
    CHECK_EQ(eve::filesystem::HotReload::normalizePath("x/"), std::string("x"));
}

TEST_CASE("hotreload.tryReloadParticleJson") {
    useIdentity("ev_ut_hot_particle");
    auto *f = fs();
    const char *name = "hot_fx.json";
    const char *json1 = R"({"emissionRate":5,"particleLifetime":[1,1]})";
    f->write(name, json1, std::strlen(json1));

    auto *mod = eve::particles::Particles::create();
    auto *e = mod->newEmitter(16);
    REQUIRE(e->loadConfig(name));
    CHECK(std::abs(e->getEmissionRate() - 5.f) < 1e-4f);

    auto *hot = eve::filesystem::HotReload::create();
    const char *json2 = R"({"emissionRate":42,"particleLifetime":[1,1]})";
    f->write(name, json2, std::strlen(json2));
    CHECK(hot->tryReload(name));
    CHECK(std::abs(e->getEmissionRate() - 42.f) < 1e-4f);

    // Prefixed path still matches after normalize.
    e->resource()->modtime = 0;
    const char *json3 = R"({"emissionRate":9,"particleLifetime":[1,1]})";
    f->write(name, json3, std::strlen(json3));
    CHECK(hot->tryReload(std::string("./") + name));
    CHECK(std::abs(e->getEmissionRate() - 9.f) < 1e-4f);

    f->remove(name);
}

TEST_CASE("hotreload.softScriptReloadPreservesState") {
    // Soft reload redefines callbacks without resetting guarded globals.
    static const char *kScript = R"SQ(
reload_count <- 0
if (!("counter" in getroottable()))
    counter <- 0
counter = counter + 1

eve_update <- function(dt) { reload_count = reload_count + 1; }

function soft_reload() {
    // Simulate load.nut soft reload of the same source.
    if (!("counter" in getroottable()))
        counter <- 0
    // guarded: do not reset counter
    eve_update <- function(dt) { reload_count = reload_count + 10; }
    if ("eve_reload" in getroottable())
        eve_reload();
}

eve_reload <- function() { counter = counter + 100; }
soft_reload();
)SQ";

    class SoftReloadTest : public ScriptTest {
    public:
        SoftReloadTest() : ScriptTest(kScript) {}
        using ScriptTest::vm;
    };

    SoftReloadTest t;
    // First load increments counter once; eve_reload from soft_reload adds 100.
    CHECK_EQ(t.vm.find("counter").toInt(), 101);
    // Updated eve_update from soft_reload uses +10
    t.vm.callFunc(t.vm.findFunc("eve_update"), t.vm, 0.016f);
    CHECK_EQ(t.vm.find("reload_count").toInt(), 10);
}

TEST_CASE("hotreload.bindAndUnbind") {
    auto *hot = eve::filesystem::HotReload::create();
    hot->bind("foo/bar.png", "texture");
    hot->unbind("foo/bar.png");
    // Unbound unknown image: tryReload returns false (no cached texture / emitters).
    CHECK(!hot->tryReload("foo/bar.png"));
}
