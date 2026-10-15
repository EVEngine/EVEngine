#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("scene.editor.script_session_owns_target_and_projects_transaction_results") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        function checked(result) { if (!result.ok) throw result.status.summary; return result; }
        editor <- eve.Editor();
        module <- eve.SceneEditorModule();
        created <- checked(module.createSession("test.scene"));
        session <- created.value;
        checked(session.execute("scene.object.create.v1",{object="box"}));
        saved <- session.saveJson();
        checked(session.execute("scene.object.update.v1",{object="box",name="Crate",position=[1.0,2.0,3.0]}));
        checked(session.undo());
        if (saved != session.saveJson()) throw "undo failed";
        checked(session.restrictCommands(["scene.transform.set.v1"]));
        refused <- session.execute("scene.object.delete.v1",{object="box"});
        checked(session.execute("scene.transform.set.v1",{object="box",position=[2.0,0.0,0.0]}));
        state <- checked(session.snapshot()).value;
    )"));
    REQUIRE(vm.find("created").toTable().get<bool>("ok"));
    REQUIRE(!vm.find("refused").toTable().get<bool>("ok"));
    REQUIRE_EQ(vm.find("state").toTable().get<int>("schemaVersion"), 1);
}

TEST_CASE("scene.editor.script_physics_placement_previews_and_commits") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        function checked(result) { if (!result.ok) throw result.status.summary; return result; }
        editor <- eve.Editor();
        module <- eve.SceneEditorModule();
        session <- checked(module.createSession("test.physics-placement")).value;
        checked(session.execute("scene.object.create.v1",{object="wall",position=[2.0,0.5,0.0]}));
        checked(session.execute("scene.object.create.v1",{object="crate",position=[0.0,0.5,0.0]}));
        checked(session.cachePhysicsPlacementCompound("crate","crate-mesh-v1",[
            {shape="box",source="generated",halfExtents=[0.25,0.5,0.5],localPosition=[-0.25,0.0,0.0]},
            {shape="box",source="generated",halfExtents=[0.25,0.5,0.5],localPosition=[0.25,0.0,0.0]}
        ]));
        colliderCache <- session.savePhysicsPlacementColliderCacheJson();
        checked(session.restorePhysicsPlacementColliderCacheJson(colliderCache));
        before <- session.saveJson();
        checked(session.beginPhysicsPlacement({objects=[
            {object="wall",selected=false,halfExtents=[0.5,2.0,2.0]},
            {object="crate",selected=true,shape="auto",colliderResourceKey="crate-mesh-v1",
             halfExtents=[0.5,0.5,0.5],layerBits=1,tags=["placeable"]}
        ],settings={colliderPolicy="generatedWhenMissing",maximumAngularSpeed=20.0,
                    scaleSpeedByBounds=true,teleportDistance=0.0},
           admission={includedLayerBits=1,excludedLayerBits=0,requiredTags=[]}}));
        for(local i=0;i<90;i++) frame <- checked(session.updatePhysicsPlacement(4.0,0.5,0.0,1.0/60.0)).value;
        if (!session.isPhysicsPlacementActive() || before != session.saveJson()) throw "preview leaked";
        checked(session.commitPhysicsPlacement());
        if (session.isPhysicsPlacementActive() || before == session.saveJson()) throw "commit failed";
        checked(session.undo());
        if (before != session.saveJson()) throw "undo failed";
        checked(session.beginPhysicsPlacement({primary="crate",objects=[
            {object="wall",selected=false,halfExtents=[0.5,2.0,2.0]},
            {object="crate",selected=true,halfExtents=[0.5,0.5,0.5]}
        ]}));
        scaled <- checked(session.updatePhysicsPlacementTransform(
            [0.0,0.5,0.0],[0.0,0.0,0.5],[2.0,1.5,0.75],1.0/60.0)).value;
        if (scaled.objects[1].scale[0] < 1.99) throw "scale preview failed";
        checked(session.cancelPhysicsPlacement());
        checked(session.beginPhysicsPlacement({mode="point",settings={freezePositionX=true,freezePositionY=true,
            freezePositionZ=true,freezeRotationX=false,freezeRotationY=false,freezeRotationZ=false},objects=[
            {object="crate",selected=true,halfExtents=[0.5,0.5,0.5]}
        ]}));
        pointed <- checked(session.updatePhysicsPlacement(5.0,0.5,0.0,1.0/60.0)).value;
        if (pointed.objects[0].position[0] > 0.01) throw "point moved locked object";
        checked(session.cancelPhysicsPlacement());
        checked(session.beginPhysicsPlacement({objects=[
            {object="wall",selected=false,halfExtents=[0.5,2.0,2.0]},
            {object="crate",selected=true,halfExtents=[0.5,0.5,0.5]}
        ]}));
        surface <- checked(session.alignPhysicsPlacementToSurface(
            [5.0,0.5,0.0],[-5.0,0.5,0.0],0.02,1.0/60.0)).value;
        if (!surface.surfaceAligned || surface.surfaceNormal[0] < 0.99) throw "surface align failed";
        checked(session.cancelPhysicsPlacement());
        checked(session.execute("scene.transform.set.v1",{object="crate",position=[0.0,3.0,0.0]}));
        checked(session.beginPhysicsPlacement({mode="drop",settings={gravityY=-9.8,subStepCount=4,
            preserveSelectionLayout=true},objects=[
            {object="wall",selected=false,halfExtents=[5.0,0.5,5.0]},
            {object="crate",selected=true,halfExtents=[0.5,0.5,0.5]}
        ]}));
        for(local i=0;i<240 && session.isPhysicsPlacementActive();i++) {
            dropped <- checked(session.updatePhysicsPlacement(0.0,0.0,0.0,1.0/60.0)).value;
            if (dropped.settled) checked(session.commitPhysicsPlacement());
        }
        if (session.isPhysicsPlacementActive() || !dropped.settled) throw "drop did not settle";
    )"));
}
