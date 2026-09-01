#include "common/Module.h"
#include "common/Runtime.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <squirrel.h>

using namespace eve;

namespace {

bool eveRawHas(HSQUIRRELVM vm, const char* key) {
    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    sq_pushstring(vm, "eve", -1);
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return false;
    }
    sq_pushstring(vm, key, -1);
    const bool has = SQ_SUCCEEDED(sq_rawget(vm, -2));
    sq_settop(vm, top);
    return has;
}

}  // namespace

TEST_CASE("moduleExpose.defersNativeClassUntilFirstGet") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);
    HSQUIRRELVM vm = runtime.handle();
    CHECK(!eveRawHas(vm, "Graphics"));
    CHECK(!eveRawHas(vm, "Window"));
    CHECK(!eveRawHas(vm, "WindowSettings"));

    runtime.runSource("g <- eve.Graphics\n", "lazy-graphics.nut");
    CHECK(eveRawHas(vm, "Graphics"));
    CHECK(!eveRawHas(vm, "Window"));

    runtime.runSource("s <- eve.WindowSettings()\n", "lazy-window-settings.nut");
    CHECK(eveRawHas(vm, "Window"));
    CHECK(eveRawHas(vm, "WindowSettings"));
}

TEST_CASE("moduleExpose.eagerCompatibilityKeepsCanonicalSpriteBinding") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    runtime.runSource(
        "if (eve._bindAllNativeClasses() < 0) throw \"native binding failed\"\n"
        "sprite <- eve.Sprite2D()\n"
        "sprite.setBlend(\"alpha\")\n"
        "sprite.setAnchor(0.5, 0.5)\n"
        "class CompatSystem extends eve.System {\n"
        "  function update(dt) { return dt + 1 }\n"
        "}\n"
        "compatSystem <- CompatSystem()\n"
        "if (compatSystem.update(2) != 3) throw \"script ECS System was replaced\"\n"
        "if (\"getPlatform\" in compatSystem) throw \"native System leaked into script ECS\"\n",
        "eager-native-binding.nut");
}
