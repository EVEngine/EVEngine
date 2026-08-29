#include "common/Runtime.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;

namespace {

const char* kReflectScript = R"SQ(
class ReflectScriptBase {
    label = "base"
}

class ReflectScriptHero extends ReflectScriptBase {
    </ editor = "slider", min = 0, max = 100 />
    hp = 100.0
    name = "Hero"
    alive = true
    tags = ["a", "b"]
    stats = { attack = 10 }
    function update(dt) { hp += dt }
}

class ReflectScriptNested {
    level = 7
}

class ReflectScriptHolder {
    nested = ReflectScriptNested()
}

function reflectScriptProbe() {
    local classes = eve.reflect.classes();
    if (!("ReflectScriptHero" in classes)) throw "classes() missing class";

    local info = eve.reflect.classInfo("ReflectScriptHero");
    if (info == null) throw "classInfo() null";
    if (info.base != "ReflectScriptBase") throw "classInfo().base mismatch";
    if (eve.reflect.classInfo("ReflectScriptMissing") != null)
        throw "classInfo() must return null for unknown class";

    local hero = eve.reflect.createInstance("ReflectScriptHero");
    if (eve.reflect.classNameOf(hero) != "ReflectScriptHero")
        throw "classNameOf() mismatch";

    local members = eve.reflect.inspect(hero);
    local hp = null;
    foreach (m in members) {
        if (m.name == "hp") hp = m;
        if (m.name == "label" && m.value != "base")
            throw "inspect() inherited member value missing";
        if (m.name == "update" && !m.method)
            throw "inspect() method flag missing";
    }
    if (hp == null) throw "inspect() missing hp";
    if (hp.attributes.editor != "slider") throw "inspect() attributes lost";
    if (hp.value != 100.0) throw "inspect() hp live value mismatch";

    if (!eve.reflect.write(hero, "hp", 42.5)) throw "write hp failed";
    if (eve.reflect.read(hero, "hp") != 42.5) throw "read hp mismatch";
    if (!eve.reflect.write(hero, "alive", false)) throw "write alive failed";
    if (eve.reflect.read(hero, "alive") != false) throw "read alive mismatch";
    if (eve.reflect.write(hero, "name", 123)) throw "typed write must convert";
    if (eve.reflect.read(hero, "name") != "123") throw "typed write conversion failed";
    if (eve.reflect.write(hero, "missing", 1))
        throw "write to missing member must fail";
    if (eve.reflect.write(hero, "update", 1))
        throw "write to method must fail";
    if (eve.reflect.write(hero, "hp", {}))
        throw "write of non-scalar must fail";

    if (eve.reflect.arraySize(hero, "tags") != 2) throw "arraySize mismatch";
    if (eve.reflect.arrayGet(hero, "tags", 0) != "a") throw "arrayGet mismatch";
    if (!eve.reflect.arrayAppend(hero, "tags", "c")) throw "arrayAppend failed";
    if (eve.reflect.arraySize(hero, "tags") != 3) throw "arrayAppend size mismatch";
    if (!eve.reflect.arraySet(hero, "tags", 2, "z")) throw "arraySet failed";
    if (eve.reflect.arrayGet(hero, "tags", 2) != "z") throw "arraySet value mismatch";
    if (!eve.reflect.arrayRemove(hero, "tags", 1)) throw "arrayRemove failed";
    if (eve.reflect.arrayGet(hero, "tags", 1) != "z")
        throw "arrayRemove shift mismatch";

    local keys = eve.reflect.tableKeys(hero, "stats");
    if (keys.len() != 1 || keys[0] != "attack") throw "tableKeys mismatch";
    if (eve.reflect.tableGet(hero, "stats", "attack") != 10)
        throw "tableGet mismatch";
    if (!eve.reflect.tableSet(hero, "stats", "defense", 5))
        throw "tableSet failed";
    if (eve.reflect.tableGet(hero, "stats", "defense") != 5)
        throw "tableSet value mismatch";
    if (!eve.reflect.tableRemove(hero, "stats", "attack"))
        throw "tableRemove failed";
    if ("attack" in eve.reflect.read(hero, "stats"))
        throw "tableRemove leftover";

    local holder = eve.reflect.createInstance("ReflectScriptHolder");
    local nested = eve.reflect.readObject(holder, "nested");
    if (nested == null) throw "readObject null";
    if (eve.reflect.classNameOf(nested) != "ReflectScriptNested")
        throw "readObject class mismatch";
    if (eve.reflect.read(nested, "level") != 7) throw "nested read mismatch";
    if (eve.reflect.read(hero, "missing") != null)
        throw "read of missing member must be null";

    // Classes defined outside the Runtime API are picked up by scan().
    compilestring("class ReflectScriptScanProbe { x = 1 }")();
    if (eve.reflect.scan() < 1) throw "scan() returned nothing";
    if (eve.reflect.classInfo("ReflectScriptScanProbe") == null)
        throw "scan() did not pick up class";

    local scripts = eve.reflect.scripts();
    if (scripts.len() == 0) throw "scripts() empty";
    if (scripts[0].source != "reflect-script-probe.nut")
        throw "scripts() source mismatch";
    if (scripts[0].state != "Loaded") throw "scripts() state mismatch";

    return true;
}
)SQ";

void requireRun(Runtime& runtime, const char* source, const char* name) {
    bool ok = false;
    try {
        runtime.runSource(source, name);
        ok = true;
    } catch (const ScriptException&) {
    }
    REQUIRE(ok);
}

}  // namespace

TEST_CASE("reflectScriptExposesRuntimeApi") {
    Runtime runtime(1024, ssq::Libs::ALL);
    runtime.initialize();
    requireRun(runtime, kReflectScript, "reflect-script-probe.nut");

    auto                scope = runtime.enter();
    const ssq::Function probe = runtime.vm().findFunc("reflectScriptProbe");
    CHECK(runtime.vm().callFunc(probe, runtime.root()).toBool());
}
