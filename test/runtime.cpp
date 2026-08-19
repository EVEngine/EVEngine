#include "common/Runtime.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <algorithm>

using namespace eve;

TEST_CASE("runtime.scopeAndNativeStackAreRestored") {
    Runtime runtime(256, ssq::Libs::ALL);
    CHECK(Runtime::current() == nullptr);
    const SQInteger top = runtime.vm().getTop();
    {
        auto scope = runtime.enter();
        CHECK(Runtime::current() == &runtime);
        CHECK_EQ(Runtime::stackDepth(), size_t(1));
        {
            auto guard = runtime.guard();
            sq_pushinteger(runtime.handle(), 42);
            CHECK_EQ(runtime.vm().getTop(), top + 1);
        }
        CHECK_EQ(runtime.vm().getTop(), top);
    }
    CHECK(Runtime::current() == nullptr);
}

TEST_CASE("runtime.reflectsAndUnloadsScriptClasses") {
    Runtime runtime(512, ssq::Libs::ALL);
    const char* source = R"SQ(
class RuntimeProbe {
    </ editor = "slider", min = 0 />
    speed = 3
    function update(dt) { speed += dt }
}
)SQ";

    const Runtime::ScriptId id = runtime.runSource(source, "runtime-probe.nut");
    const ScriptInfo* script = runtime.script(id);
    CHECK(script != nullptr);
    // zeroerr's CHECK cannot print the ScriptState enum; compare via int.
    CHECK(static_cast<int>(script->state) == static_cast<int>(ScriptState::Loaded));
    CHECK(std::find(script->classes.begin(), script->classes.end(), "RuntimeProbe") !=
          script->classes.end());

    const ReflectedClass* cls = runtime.reflectedClass("RuntimeProbe");
    CHECK(cls != nullptr);
    CHECK_EQ(cls->source, std::string("runtime-probe.nut"));
    auto speed = std::find_if(cls->members.begin(), cls->members.end(),
                              [](const ReflectedMember& member) { return member.name == "speed"; });
    CHECK(speed != cls->members.end());
    CHECK(!speed->method);
    CHECK(std::find_if(speed->attributes.begin(), speed->attributes.end(),
                       [](const ReflectedAttribute& attribute) {
                           return attribute.name == "editor" && attribute.value == "slider";
                       }) != speed->attributes.end());

    CHECK(runtime.unload(id));
    CHECK(runtime.reflectedClass("RuntimeProbe") == nullptr);
    bool removed = false;
    try {
        runtime.findClass("RuntimeProbe");
    } catch (const ssq::NotFoundException&) {
        removed = true;
    }
    CHECK(removed);
}

TEST_CASE("runtimeWrapsCompileErrorsAndRestoresStack") {
    Runtime runtime(256, ssq::Libs::ALL);
    const SQInteger top = runtime.vm().getTop();
    bool caught = false;
    try {
        runtime.compileSource("class Broken {", "broken.nut");
    } catch (const ScriptException& error) {
        caught = true;
        // zeroerr's CHECK_EQ cannot print the ScriptStage enum; compare via int.
        CHECK_EQ(static_cast<int>(error.stage()), static_cast<int>(ScriptStage::Compile));
        CHECK_EQ(error.source(), std::string("broken.nut"));
    }
    CHECK(caught);
    CHECK_EQ(runtime.vm().getTop(), top);
}
