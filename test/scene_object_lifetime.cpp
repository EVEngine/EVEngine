#include <simplesquirrel/simplesquirrel.hpp>
#include "common/ECS.h"
#include "common/Module.h"
#include "common/Runtime.h"
#include "scene/SceneObject.h"
#include "zeroerr/unittest.h"

TEST_CASE("Scene.script.firstObjectLookupAndDelayedDetach") {
    ecs::Table       table;
    ecs::ScopedTable current(table);
    eve::Runtime     runtime;
    eve::ModuleManager::expose(runtime);
    const auto script = runtime.runSource(R"SQ(
        if (eve._ensureScriptECS() < 0) throw "script ECS initialization failed";
        local host = eve.Scene();
        class Pulse extends eve.SceneEntity {
            ticks = 0;
            detached = false;
            function update(dt) {
                ticks += 1;
                if (!scene().scheduleDetachEntity(nodeId, this)) throw "detach failed";
            }
            function onDetach() { detached = true; }
        }
        host.beginBuild();
        host.beginNode("root", "Root");
        host.addNode("marker", "Marker");
        host.end();
        if (!host.mountBuildAs("first-slot")) throw "mount failed";
        local entity = host.attachEntity("marker", Pulse);
        if (entity == null || host.getEntity("marker", Pulse) != entity) throw "lookup failed";
        local callbacks = 0;
        host.forEachEntity("first-slot", "marker", function(instance, index) {
            if (instance != entity || index != 0) throw "callback arguments failed";
            callbacks += 1;
        });
        if (callbacks != 1) throw "callback was not invoked";
        if (host.getEntityAt("first-slot", "marker", Pulse) != entity) throw "host lookup failed";
        local entities = host.entitiesOfAt("first-slot", "marker");
        if (entities.len() != 1 || entities[0] != entity) throw "host enumeration failed";
        entity.disable();
        host.update(0.016);
        if (entity.ticks != 0) throw "disabled entity updated";
        entity.enable();
        host.update(0.016);
        host.update(0.016);
        if (entity.ticks != 1 || !entity.detached) throw "delayed detach failed";
        if (host.getEntity("marker", Pulse) != null) throw "detached entity still rooted";
        host.beginBuild();
        host.beginNode("root", "Root");
        host.addNode("marker", "Marker");
        host.end();
        if (!host.remountBuildAs("first-slot")) throw "remount failed";
        local replacement = host.attachEntity("marker", Pulse);
        if (replacement == null || host.getEntity("marker", Pulse) != replacement) throw "replacement lookup failed";
        host.update(0.016);
        if (!replacement.detached) throw "replacement detach failed";
        local orphan = host.attachEntity("marker", Pulse);
        if (orphan == null) throw "orphan attach failed";
        host.beginBuild();
        host.beginNode("root", "Root");
        host.end();
        if (!host.remountBuildAs("first-slot")) throw "host replacement failed";
        if (!orphan.detached) throw "host-first teardown did not detach its entity";
    )SQ");
    CHECK(script != 0);
    CHECK(table.getManager<eve::scene::SceneObject>() != nullptr);
}
