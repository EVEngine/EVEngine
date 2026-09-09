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
