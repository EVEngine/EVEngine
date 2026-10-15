#include <simplesquirrel/simplesquirrel.hpp>
#include "common/Module.h"
#include "scene/editor/SceneEditorModule.h"
#include "scripts.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("scene.authoring.script_owns_document_and_rejects_bad_bounds") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(vm);
    vm.run(vm.compileSource(R"(
        editor <- eve.Editor();
        module <- eve.SceneEditorModule();
        function checked(r) {if(!r.ok) throw r.status.summary;return r.value;}
        session <- checked(module.createAuthoringSession("script-authoring"));
        spawned <- session.execute("scene.authoring.spawn.v1",{object="box",source={kind="box"}});
        if(!spawned.ok) throw spawned.status.summary;
        saved <- session.saveJson();
        snapshot <- checked(session.snapshot());
        queried <- module.inspectAuthoringSnapshot(snapshot,"box",[-0.5,-0.5,-0.5,0.5,0.5,0.5]);
        if(!queried.ok || !queried.hasValue || queried.value.worldCorners.len()!=8) throw "query failed";
        bad <- module.inspectAuthoringSnapshot(snapshot,"box",["bad",0,0,1,1,1]);
        if(bad.ok || saved!=session.saveJson()) throw "invalid bounds mutated state";
    )"));
    REQUIRE(vm.find("spawned").toTable().get<bool>("ok"));
    REQUIRE(!vm.find("bad").toTable().get<bool>("ok"));
}

TEST_CASE("scene.authoring.director_compiles_and_missing_native_provider_is_observable") {
    ssq::VM vm(2048, ssq::Libs::ALL);
    bool    refused = false;
    auto    script  = vm.compileSource(eve::scene_director_content);
    try {
        vm.run(script);
    } catch (const std::exception&) {
        refused = true;
    }
    REQUIRE(refused);
}
