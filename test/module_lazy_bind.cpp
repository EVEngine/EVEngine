#include "common/Module.h"
#include "common/Runtime.h"
#include "Fixtures.h"

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

    runtime.runSource("if (eve._bindAllNativeClasses() < 0) throw \"native binding failed\"\n",
        "eager-native-binding-setup.nut");
    CHECK(eveRawHas(runtime.handle(), "_ecsSlotsCache"));
    CHECK(eveRawHas(runtime.handle(), "_ecsTypes"));

    runtime.runSource(
        "sprite <- eve.Sprite2D()\n"
        "sprite.setBlend(\"alpha\")\n"
        "sprite.setAnchor(0.5, 0.5)\n"
        "class CompatComponent extends eve.Component { value = 1 }\n"
        "class CompatEntity extends eve.Entity { data = CompatComponent }\n"
        "compatEntity <- CompatEntity.create()\n"
        "if (compatEntity.data.value != 1) throw \"script ECS component creation failed\"\n"
        "if (eve.view(CompatEntity).len() != 1) throw \"script ECS view failed\"\n"
        "class CompatSystem extends eve.System {\n"
        "  function update(dt) { return dt + 1 }\n"
        "}\n"
        "compatSystem <- CompatSystem()\n"
        "if (compatSystem.update(2) != 3) throw \"script ECS System was replaced\"\n"
        "if (\"getPlatform\" in compatSystem) throw \"native System leaked into script ECS\"\n"
        "class CompatEntity extends eve.Entity {\n"
        "  function update(dt) { return dt + 2 }\n"
        "}\n"
        "compatEntity <- CompatEntity()\n"
        "if (compatEntity.update(2) != 4) throw \"script ECS Entity identity changed\"\n"
        "vehicle <- eve.Vehicle()\n"
        "if (!(\"update\" in vehicle)) throw \"Vehicle binding lost update\"\n"
        "scene <- eve.Scene()\n"
        "if (!(\"update\" in scene)) throw \"Scene binding lost update\"\n",
        "eager-native-binding.nut");
}

TEST_CASE("moduleExpose.lazyGraphicsPreservesReturnedSpriteSurface") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    runtime.runSource("g <- eve.Graphics()\n", "lazy-graphics-instance.nut");
    REQUIRE(ModuleManager::expose_pending() >= 0);
    runtime.runSource(R"(
        s <- g.newSprite2D()
        if (!(s instanceof eve.Sprite2D)) throw "wrong Sprite2D class after pending exposers"
        s.setBlend("alpha")
        s.setAnchor(0.25, 0.75)
        s.destroy()
    )", "lazy-graphics-sprite.nut");
}

TEST_CASE("moduleExpose.allBindingsPreserveScriptEcsOverrides") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    REQUIRE(ModuleManager::expose_pending() >= 0);
    runtime.runSource(R"(
        class LazyPosition extends eve.Component { x = 0.0 }
        class LazyEntity extends eve.Entity { position = LazyPosition }
        class LazySystem extends eve.System {
            constructor() { base.constructor(LazyEntity) }
            function update(dt) {
                foreach (entity in entities()) entity.position.x += dt
            }
        }
        if (typeof eve._ecsSlotsCache != "table") throw "ECS slot cache was replaced"
        if (!(eve.isSubclass(LazyEntity, eve.Entity))) throw "ECS entity inheritance was lost"
        if (!(eve.isSubclass(LazyPosition, eve.Component))) throw "ECS component inheritance was lost"
        if (!(eve.isSubclass(LazySystem, eve.System))) throw "ECS system inheritance was lost"
        local lazyFields = eve.inspectClassFields(LazyEntity)
        if (typeof lazyFields != "table") throw "ECS field inspection was replaced"
        if (!("position" in lazyFields)) throw "ECS entity field inspection lost position"
        if (lazyFields.position != LazyPosition) throw "ECS entity field inspection changed position"
        if (!(eve._ecsIsComponentClass(lazyFields.position))) throw "ECS component classification failed"
        lazyEntity <- LazyEntity.create()
        lazySystem <- LazySystem()
        lazySystem.update(0.5)
        if (lazyEntity.position.x != 0.5) throw "script ECS override was lost"
        hostSystem <- eve.HostSystem()
        if (hostSystem.getName() != "System") throw "host System module binding was lost"
    )", "lazy-script-ecs.nut");
}

TEST_CASE("moduleExpose.allBindingsPreserveRenderable3DSurface") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    runtime.runSource("g <- eve.Graphics()\n", "lazy-graphics-3d-instance.nut");
    REQUIRE(ModuleManager::expose_pending() >= 0);
    runtime.runSource(R"(
        renderable <- eve.Renderable3D()
        renderable.clearMeshLod()
        renderable.setPosition(1.0, 2.0, 3.0)
        renderable.setRoughness(0.8)
        renderable.setCastShadow(true)
        renderable.setReceiveShadow(true)
    )", "lazy-graphics-3d.nut");
}

TEST_CASE("moduleExpose.allBindingsPreserveProcgenRockSurface") {
    eve::graphics::Graphics* graphics = nullptr;
    openHeadlessGfx(graphics, 128, 128);
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    REQUIRE(ModuleManager::expose_pending() >= 0);
    runtime.runSource(R"(
        procgen <- eve.Procgen()
        paramsResult <- procgen.newParams()
        if (!paramsResult.ok) throw "Procgen parameter creation failed"
        rockParams <- paramsResult.value
        rockParams.setSeed(1847)
        rockParams.setInt("subdivisions", 3)
        rockParams.setString("baseShape", "mixed")
        rockParams.setFloat("variation", 0.48)
        rockParams.setFloat("radius", 0.72)
        rockParams.setSize(256, 256)
        gfx <- eve.Graphics()
        meshResult <- procgen.generateMesh("mesh.rock", rockParams, gfx)
        if (!meshResult.ok) throw "Procgen rock mesh generation failed"
        textureResult <- procgen.generateTexture("tex.stone", rockParams, gfx)
        if (!textureResult.ok) throw "Procgen stone texture generation failed"
        normalResult <- procgen.generateNormalImage("tex.stone", rockParams)
        if (!normalResult.ok) throw "Procgen normal image generation failed"
        normalTexture <- gfx.newTexture(normalResult.value, true, true)
        if (normalTexture == null) throw "Procgen normal texture upload failed"
        camera <- eve.Camera3D()
        camera.setEye(3.25, 2.15, 5.1)
        camera.setTarget(-0.4, -0.1, 0.0)
        camera.setFov(37.0)
    )", "lazy-procgen-rock.nut");
}

TEST_CASE("moduleExpose.allBindingsPreserveVehicleFrameSurface") {
    Runtime runtime(1024, ssq::Libs::ALL);
    ModuleManager::expose(runtime);

    REQUIRE(ModuleManager::expose_pending() >= 0);
    runtime.runSource(R"(
        vehicle <- eve.Vehicle()
        vehicle.registerVehiclesFromJson(
            "[{\"id\":\"probe\",\"mobility\":\"kinematic\",\"maxSpeed\":80,\"accel\":60}]"
        )
        probeVehicle <- vehicle.newVehicle("probe", 0.0, 0.0, 0.0, "probe")
        if (!(probeVehicle instanceof eve.VehicleEntity)) throw "Vehicle entity binding was replaced"
        vehicle.setPosition(probeVehicle, 1.0, 2.0)
        if (vehicle.getX(probeVehicle) != 1.0 || vehicle.getY(probeVehicle) != 2.0)
            throw "Vehicle state binding was lost"
        vehicle.moveTo(probeVehicle, 10.0, 0.0)
        vehicle.update(0.1)
        local visible = false
        foreach (candidate in eve.view(eve.VehicleEntity)) {
            if (candidate.getId() == probeVehicle.getId()) visible = true
        }
        if (!visible) throw "Vehicle ECS view binding was lost"
    )", "lazy-vehicle-frame.nut");
}
