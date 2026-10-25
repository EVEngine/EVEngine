// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "tests",
  "default": "script-binding",
  "module": "physics"
}
*/
// clang-format on

#include <filesystem>
#include <random>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelOwnership.h"
#include "physics/Physics.h"
#include "physics/backend/bake/script/PhysicsBake.h"
#include "physics/bake_recording/PhysicsBakeRecording.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/cloth/ClothModule.h"
#include "physics/rope/Rope.h"
#include "physics/rope/Rope3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace {
thread_local int destroyedClothSources = 0;
thread_local int destroyedRopeSources  = 0;
template <class T>
SQRELEASEHOOK& sourceReleaseHook() {
    static thread_local SQRELEASEHOOK hook = nullptr;
    return hook;
}
template <class T>
SQInteger countedSourceRelease(SQUserPointer pointer, SQInteger size) noexcept {
    if constexpr (std::is_same_v<T, eve::physics::Cloth3D>)
        ++destroyedClothSources;
    else
        ++destroyedRopeSources;
    return sourceReleaseHook<T>()(pointer, size);
}
template <class T>
bool watchOwnedSource(const ssq::Object& object) {
    auto vm = object.getHandle();
    sq_pushobject(vm, object.getRaw());
    const auto original = sq_getreleasehook(vm, -1);
    const bool owns     = original != nullptr && original != countedSourceRelease<T>;
    if (owns) {
        sourceReleaseHook<T>() = original;
        sq_setreleasehook(vm, -1, countedSourceRelease<T>);
    }
    sq_pop(vm, 1);
    return owns;
}
}  // namespace
TEST_CASE("physics.bake.scriptRecordsLiveClothAndRopeThenReplaysWithoutSources") {
    const auto dir =
        std::filesystem::temp_directory_path() / ("eve-record-script-" + std::to_string(std::random_device{}()));
    REQUIRE(std::filesystem::create_directory(dir));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{dir};
    destroyedClothSources = destroyedRopeSources = 0;
    ssq::VM vm(2048);
    auto    table = vm.addTable("eve");
    eve::physics::Physics::expose(table);
    eve::cloth::Cloth::expose(table);
    eve::physics::Rope::expose(table);
    vm.addFunc("watchCloth", &watchOwnedSource<eve::physics::Cloth3D>);
    vm.addFunc("watchRope", &watchOwnedSource<eve::physics::Rope3D>);
    vm.addFunc("destroyedCloth", [] { return destroyedClothSources; });
    vm.addFunc("destroyedRope", [] { return destroyedRopeSources; });
    vm.addFunc("assetPath", [&](const std::string& kind) { return (dir / (kind + ".bake")).string(); });
    vm.run(vm.compileSource(R"(
        foreach (kind in ["cloth", "rope"]) {
            local recipe = eve.BakeRecipe();
            assert(recipe.setProducer("live-script-recording-test").ok);
            assert(recipe.setBackend("cpu-test").ok);
            assert(recipe.setParameters("gravity-default").ok);
            assert(recipe.setSource("fixture", kind).ok);
            assert(recipe.setTiming(1.0 / 60.0, 30).ok);
            local capture = eve.Physics().bakeRecording();
            assert(!capture.beginDeform(null, 32).ok);
            assert(!capture.beginDeform(recipe, 0).ok);
            assert(!capture.beginDeform(recipe, 2000001).ok);
            local made = capture.beginDeform(recipe, 32);
            assert(made.ok && made.hasValue);
            local session = made.value;
            assert(!session.save(assetPath(kind)).ok);
            local source = kind == "cloth" ? eve.Cloth().newCloth3D(3,3,1.0,0.0,3.0,0.0)
                : eve.Rope().newRope3D(4,0.0,3.0,0.0,3.0,3.0,0.0);
            assert(kind == "cloth" ? watchCloth(source) : watchRope(source));
            assert(!source.captureBake(null,0.0).ok);
            assert(!source.captureBake(recipe,0.0).ok);
            assert(!source.captureBake(session,-1.0).ok);
            if (kind == "cloth") {
                source.unpin(0); source.unpin(1); source.unpin(2);
            }
            assert(source.captureBake(session,0.0).ok);
            for (local i=0; i<30; ++i) source.update(1.0 / 60.0);
            if (kind == "cloth") {
                assert(!source.captureBake(session,0.1).ok);
                assert(source.captureBake(session,0.5).ok);
                source.tearConstraint(0,1);
                assert(!source.captureBake(session,1.0).ok);
            } else {
                assert(!source.captureBake(session,0.1).ok);
                assert(source.captureBake(session,0.5).ok);
                assert(source.cut(1));
                assert(!source.captureBake(session,1.0).ok);
            }
            assert(session.frameCount() == 2);
            assert(session.sampleCount() == (kind == "cloth" ? 18 : 8));
            source = null; collectgarbage();
            assert(kind == "cloth" ? destroyedCloth() == 1 : destroyedRope() == 1);
            assert(recipe.setSource("fixture", "changed after admission").ok);
            assert(session.save(assetPath(kind)).ok);
            assert(!session.save(assetPath(kind)).ok);
            assert(!eve.Physics().bake().loadDeform(assetPath(kind),recipe).ok);
            assert(recipe.setSource("fixture",kind).ok);
            local loaded = eve.Physics().bake().loadDeform(assetPath(kind),recipe);
            assert(loaded.ok && loaded.hasValue);
            made = null; session = null; recipe = null; collectgarbage();
            local player = loaded.value;
            assert(player.duration() == 0.5 && player.seek(0.25).ok);
            local frame = player.captureSample().value;
            assert(frame.vertexCount() == (kind == "cloth" ? 9 : 4));
            assert(frame.primitive() == (kind == "cloth" ? "triangles" : "segments"));
            assert(frame.point(0).value.y < 3.0);
        }
        local jsonSource = eve.Rope().newRope3DFromJson(
            "{\"particleCount\":4,\"startX\":0,\"startY\":3,\"startZ\":0,\"endX\":3,\"endY\":3,\"endZ\":0}");
        assert(watchRope(jsonSource));
        local alias = jsonSource;
        jsonSource = null; collectgarbage(); assert(destroyedRope() == 1);
        alias.update(1.0/60.0);
        alias = null; collectgarbage(); assert(destroyedRope() == 2);
    )"));
}

TEST_CASE("physics.bake.scriptRigidRecordingUsesGenerationalBindings") {
    const auto dir =
        std::filesystem::temp_directory_path() / ("eve-rigid-script-" + std::to_string(std::random_device{}()));
    REQUIRE(std::filesystem::create_directory(dir));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{dir};
    ssq::VM vm(4096);
    auto    table = vm.addTable("eve");
    eve::physics::Physics::expose(table);
    vm.addFunc("assetPath", [&](bool planar) { return (dir / (planar ? "2d.rigid" : "3d.rigid")).string(); });
    vm.run(vm.compileSource(R"(
        foreach (planar in [true,false]) {
            local recipe = eve.BakeRecipe();
            assert(recipe.setCoordinates(planar ? "pixels2d" : "metres3d").ok);
            assert(recipe.setProducer("live-rigid-script-test").ok);
            assert(recipe.setBackend(planar ? "box2d-test" : "box3d-test").ok);
            assert(recipe.setParameters("zero-gravity;linear-velocity").ok);
            assert(recipe.setSource("body", planar ? "rectangle" : "box").ok);
            assert(recipe.setTiming(1.0/60.0,30).ok);
            local capture = eve.Physics().bakeRecording();
            assert(!capture.beginRigid(null,8).ok && !capture.beginRigid(recipe,0).ok);
            local session = capture.beginRigid(recipe,8).value;
            local physics = eve.Physics();
            local world = planar ? physics.newWorld(0.0,0.0,false) : physics.newWorld3D(0.0,0.0,0.0,false);
            local foreign = planar ? physics.newWorld(0.0,0.0,false) : physics.newWorld3D(0.0,0.0,0.0,false);
            if (planar) world.setMeter(100.0);
            local body = planar ? world.newBody("dynamic",20.0,30.0) : world.newBody("dynamic",2.0,3.0,4.0);
            local id = "00000000-0000-0000-0000-000000000011";
            local shape = null;
            if (planar) {
                shape = body.newRectangleFixture(10.0,10.0,1.0,0.2,0.0);
                body.setLinearVelocity(40.0,0.0);
                assert(!session.capture2D(world,0.0).ok);
                assert(!session.bindBody3D(world,body,id).ok);
                assert(!session.bindBody2D(foreign,body,id).ok);
                assert(!session.bindBody2D(world,body,"invalid").ok);
                assert(session.bindBody2D(world,body,id).ok);
                assert(!session.bindBody2D(world,body,id).ok);
                assert(session.capture2D(world,0.0).ok);
                assert(!session.bindBody2D(world,body,id).ok);
            } else {
                shape = body.newBoxShape(1.0,1.0,1.0,1.0,0.2,0.0);
                body.setLinearVelocity(2.0,0.0,0.0);
                assert(!session.capture3D(world,0.0).ok);
                assert(!session.bindBody2D(world,body,id).ok);
                assert(!session.bindBody3D(foreign,body,id).ok);
                assert(!session.bindBody3D(world,body,"invalid").ok);
                assert(session.bindBody3D(world,body,id).ok);
                assert(!session.bindBody3D(world,body,id).ok);
                assert(session.capture3D(world,0.0).ok);
                assert(!session.bindBody3D(world,body,id).ok);
            }
            assert(session.bindingCount() == 1);
            for (local i=0; i<30; ++i) world.update(1.0/60.0);
            if (planar) {
                assert(!session.capture2D(foreign,0.5).ok);
                assert(session.capture2D(world,0.5).ok);
            } else {
                assert(!session.capture3D(foreign,0.5).ok);
                assert(session.capture3D(world,0.5).ok);
            }
            body.destroy(); shape = null; body = null;
            local replacement = planar ? world.newBody("dynamic",200.0,300.0) : world.newBody("dynamic",20.0,30.0,40.0);
            if (planar) assert(!session.capture2D(world,1.0).ok);
            else assert(!session.capture3D(world,1.0).ok);
            assert(session.frameCount() == 2 && session.sampleCount() == 2);
            world.destroy(); foreign.destroy();
            replacement = null; world = null; foreign = null; collectgarbage();
            assert(session.save(assetPath(planar)).ok);
            assert(!session.save(assetPath(planar)).ok);
            local loaded = eve.Physics().bake().loadRigid(assetPath(planar),recipe);
            assert(loaded.ok && loaded.hasValue);
            session = null; recipe = null; collectgarbage();
            local player = loaded.value;
            assert(player.seek(0.25).ok && player.trackCount() == 1);
            local pose = player.sample(id).value;
            assert(pose.x > (planar ? 29.9 : 2.4) && pose.x < (planar ? 30.1 : 2.6));
        }
    )"));
}
