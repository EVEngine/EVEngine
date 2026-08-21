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

TEST_CASE("runtimeRuntimeErrorCarriesLocationAndStack") {
    Runtime runtime(256, ssq::Libs::ALL);
    bool caught = false;
    try {
        runtime.runSource(R"SQ(
function boom() { throw "kaboom" }
function outer() { boom() }
outer();
)SQ", "boom.nut");
    } catch (const ScriptException& error) {
        caught = true;
        CHECK_EQ(static_cast<int>(error.stage()), static_cast<int>(ScriptStage::Execute));
        CHECK_EQ(error.source(), std::string("boom.nut"));
        CHECK(error.hasLocation());
        CHECK(error.line() > 0);
        CHECK(!error.function().empty());
        const std::string message = error.what();
        CHECK(message.find("kaboom") != std::string::npos);
        CHECK(message.find("boom.nut:") != std::string::npos);
        CHECK(message.find("Stack:") != std::string::npos);
        CHECK(!error.stackTrace().empty());
        CHECK(error.stackTrace().find("boom") != std::string::npos);
        CHECK(error.stackTrace().find("outer") != std::string::npos);
    }
    CHECK(caught);
}

TEST_CASE("runtimeRuntimeErrorMarksScriptFailedAndRestoresStack") {
    Runtime runtime(256, ssq::Libs::ALL);
    const SQInteger top = runtime.vm().getTop();
    const Runtime::ScriptId id = runtime.compileSource("function bad() { throw \"nope\" }\nbad();\n",
                                                       "fail.nut");
    bool caught = false;
    try {
        runtime.execute(id);
    } catch (const ScriptException&) {
        caught = true;
    }
    CHECK(caught);
    const ScriptInfo* script = runtime.script(id);
    CHECK(script != nullptr);
    CHECK(static_cast<int>(script->state) == static_cast<int>(ScriptState::Failed));
    CHECK(!script->error.empty());
    CHECK(script->error.find("fail.nut") != std::string::npos);
    CHECK_EQ(runtime.vm().getTop(), top);
}

TEST_CASE("runtimeCompileErrorCarriesLineColumnAndSnippet") {
    Runtime runtime(256, ssq::Libs::ALL);
    bool caught = false;
    try {
        runtime.compileSource("local x = ;\nlocal y = 1;\n", "broken.nut");
    } catch (const ScriptException& error) {
        caught = true;
        CHECK_EQ(static_cast<int>(error.stage()), static_cast<int>(ScriptStage::Compile));
        CHECK(error.hasLocation());
        CHECK(error.line() >= 1);
        const std::string message = error.what();
        CHECK(message.find("local x = ;") != std::string::npos);
        CHECK(message.find("broken.nut:") != std::string::npos);
    }
    CHECK(caught);
}
