#include "common/Runtime.h"
#include "common/ScriptCompiler.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace eve;

TEST_CASE("scriptCompiler.recordsErasedLanguageMetadata") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.compileSource(
        "persist score: int = 0\n"
        "export function answer(value: int) -> int { return value + score }\n",
        "game:/metadata.nut");

    const script::ScriptMetadata* metadata = runtime.scriptCompiler().metadata("game:/metadata.nut");
    CHECK(metadata != nullptr);
    CHECK(!metadata->sourceHash.empty());
    CHECK_EQ(metadata->persistRoots.size(), size_t(1));
    CHECK_EQ(metadata->persistRoots[0], std::string("score"));
    CHECK_EQ(metadata->exports.size(), size_t(1));
    CHECK_EQ(metadata->exports[0], std::string("answer"));
    CHECK_EQ(metadata->sourceMap.originalPosition({2, 9}).line, uint32_t(2));
}

TEST_CASE("scriptCompiler.bindingContractsAreReplaceableAndSorted") {
    script::BindingContractRegistry registry;
    script::BindingContract         camera;
    camera.module      = "camera";
    camera.scriptClass = "CameraController";
    camera.method      = "setYaw";
    camera.parameters.push_back({"yaw", "float", false, std::nullopt, script::ScriptUnit::Radians, {}});
    camera.returnType = "void";
    registry.registerContract(camera);

    const script::BindingContract* found = registry.find("camera/CameraController.setYaw");
    CHECK(found != nullptr);
    CHECK_EQ(found->parameters[0].name, std::string("yaw"));
    CHECK_EQ(static_cast<int>(found->parameters[0].unit), static_cast<int>(script::ScriptUnit::Radians));
    CHECK_EQ(registry.snapshot().size(), size_t(1));
    CHECK(registry.unregisterContract("camera/CameraController.setYaw"));
}

TEST_CASE("scriptCompiler.generatedModuleContractIsExecutable") {
    const std::filesystem::path generated =
        std::filesystem::path(EVENGINE_TEST_BINARY_DIR).parent_path() / "src" / "scripts" / "module_list.nut";
    std::ifstream     input(generated, std::ios::binary);
    const std::string source(std::istreambuf_iterator<char>(input), {});
    CHECK(!source.empty());

    Runtime runtime(1024, ssq::Libs::ALL);
    runtime.runSource(source, "module_list.nut");
    ssq::Array contract(runtime.root().find("eve_module_contract"));
    CHECK(contract.size() > 0);
}

TEST_CASE("scriptCompiler.bindingContractEnablesNativeNamedArguments") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeCompose",
                         [](int first, int second, int third) { return first * 100 + second * 10 + third; });
    script::BindingContract contract;
    contract.module     = "test";
    contract.method     = "nativeCompose";
    contract.parameters = {{"first", "int"}, {"second", "int"}, {"third", "int"}};
    contract.returnType = "int";
    runtime.scriptCompiler().bindings().registerContract(std::move(contract));

    runtime.runSource("native_named_result <- nativeCompose(third: 3, first: 1, second: 2)\n", "native-named-test.nut");
    const auto result = runtime.vm().get<int64_t>("native_named_result");
    CHECK_EQ(result, int64_t(123));
}

TEST_CASE("scriptCompiler.bindingContractConvertsUnitLiterals") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeAngle", [](float angle) { return angle; });
    runtime.vm().addFunc("nativeDelay", [](float delay) { return delay; });

    script::BindingContract angle;
    angle.module     = "test";
    angle.method     = "nativeAngle";
    angle.parameters = {{"angle", "float", false, std::nullopt, script::ScriptUnit::Radians}};
    runtime.scriptCompiler().bindings().registerContract(std::move(angle));
    script::BindingContract delay;
    delay.module     = "test";
    delay.method     = "nativeDelay";
    delay.parameters = {{"delay", "float", false, std::nullopt, script::ScriptUnit::Milliseconds}};
    runtime.scriptCompiler().bindings().registerContract(std::move(delay));

    runtime.runSource(
        "native_angle_result <- nativeAngle(90deg)\n"
        "native_delay_result <- nativeDelay(250ms)\n",
        "native-units-test.nut");
    const float angleResult    = runtime.vm().get<float>("native_angle_result");
    const float delayResult    = runtime.vm().get<float>("native_delay_result");
    const bool  angleConverted = angleResult > 1.5707f && angleResult < 1.5709f;
    CHECK(angleConverted);
    CHECK_EQ(delayResult, 250.0f);
}

TEST_CASE("scriptCompiler.bindingContractChecksStringChoices") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeMode", [](std::string mode) { return mode; });
    script::BindingContract mode;
    mode.module     = "test";
    mode.method     = "nativeMode";
    mode.parameters = {{"mode", "string", false, std::nullopt, script::ScriptUnit::None, {"idle", "run"}}};
    runtime.scriptCompiler().bindings().registerContract(std::move(mode));
    runtime.runSource("native_mode_result <- nativeMode(\"run\")\n", "native-choice-test.nut");
    const std::string modeResult = runtime.vm().get<std::string>("native_mode_result");
    CHECK_EQ(modeResult, std::string("run"));

    bool rejected = false;
    try {
        runtime.compileSource("nativeMode(\"broken\")\n", "native-choice-error.nut");
    } catch (const ScriptException& error) {
        rejected = std::string(error.what()).find("outside the allowed choices") != std::string::npos;
    }
    CHECK(rejected);
}

TEST_CASE("scriptCompiler.recordsInspectorPropertyMetadata") {
    const script::ScriptMetadata metadata = script::ScriptCompiler::analyze(
        "class CharacterData {\n"
        "  @editor(\"slider\", min: 0, max: 100, step: 1)\n"
        "  @unit(\"hp\")\n"
        "  hp: float = 100.0\n"
        "  @editor(\"combo\")\n"
        "  job: \"warrior\" | \"mage\" | \"rogue\" = \"warrior\"\n"
        "}\n",
        "game:/character.nut");

    CHECK_EQ(metadata.properties.size(), size_t(2));
    const script::ScriptPropertyMetadata& hp = metadata.properties[0];
    CHECK_EQ(hp.name, std::string("hp"));
    CHECK_EQ(hp.attributes.at("editor"), std::string("slider"));
    CHECK_EQ(hp.attributes.at("min"), std::string("0"));
    CHECK_EQ(hp.attributes.at("unit"), std::string("hp"));

    const script::ScriptPropertyMetadata& job = metadata.properties[1];
    CHECK_EQ(job.choices.size(), size_t(3));
    CHECK_EQ(job.attributes.at("options"), std::string("warrior,mage,rogue"));
}

TEST_CASE("scriptCompiler.retainsStructuredDiagnosticsAfterFailure") {
    Runtime runtime(512, ssq::Libs::ALL);
    bool    rejected = false;
    try {
        runtime.compileSource("function invalid() { return await 1 }\n", "game:/invalid.nut");
    } catch (const ScriptException&) {
        rejected = true;
    }
    CHECK(rejected);
    const script::ScriptMetadata* metadata = runtime.scriptCompiler().metadata("game:/invalid.nut");
    CHECK(metadata != nullptr);
    CHECK_EQ(metadata->diagnostics.size(), size_t(1));
    CHECK_EQ(metadata->diagnostics[0].code, std::string("EVE2601"));
    CHECK_EQ(metadata->diagnostics[0].position.line, uint32_t(1));
    CHECK(!metadata->diagnostics[0].fix.empty());
}

TEST_CASE("scriptCompiler.requiresPluginAnnotationsToBeRegistered") {
    Runtime runtime(512, ssq::Libs::ALL);
    bool    rejected = false;
    try {
        runtime.compileSource("class Asset { @plugin_asset(\"texture\") path = \"\" }\n", "game:/asset-error.nut");
    } catch (const ScriptException&) {
        rejected = true;
    }
    CHECK(rejected);

    runtime.scriptCompiler().registerAnnotation("plugin_asset");
    runtime.runSource("class Asset { @plugin_asset(\"texture\") path = \"\" }\n", "game:/asset.nut");
    CHECK(runtime.scriptCompiler().unregisterAnnotation("plugin_asset"));
}

TEST_CASE("scriptCompiler.servesLanguageToolingQueries") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.compileSource("local playerSpeed: float = 2.0\n", "game:/tooling.nut");
    script::BindingContract contract;
    contract.module          = "physics";
    contract.method          = "newWorld";
    contract.returnType      = "World";
    contract.documentationId = "physics.newWorld";
    contract.parameters      = {{"gravity", "float"}, {"sleep", "bool"}};
    runtime.scriptCompiler().bindings().registerContract(std::move(contract));

    const auto scriptItems = runtime.scriptCompiler().completions("game:/tooling.nut", "player");
    CHECK_EQ(scriptItems.size(), size_t(1));
    CHECK_EQ(scriptItems[0].label, std::string("playerSpeed"));
    const auto nativeItems = runtime.scriptCompiler().completions("game:/tooling.nut", "new");
    CHECK_EQ(nativeItems.size(), size_t(1));
    CHECK(nativeItems[0].insertText.find("gravity:") != std::string::npos);

    const auto hover = runtime.scriptCompiler().hover("game:/tooling.nut", "newWorld");
    CHECK(hover.has_value());
    CHECK(hover->markdown.find("physics.newWorld") != std::string::npos);
    CHECK(runtime.scriptCompiler().diagnostics("game:/tooling.nut").empty());
}

TEST_CASE("scriptCompiler.rawVmBridgeRecordsToolingMetadata") {
    Runtime         runtime(512, ssq::Libs::ALL);
    const char*     source = "local replValue: int = 7\n";
    const SQInteger top    = sq_gettop(runtime.handle());
    const SQRESULT  result = script::ScriptCompiler::compileBuffer(
        runtime.handle(), source, static_cast<SQInteger>(std::strlen(source)), "console_repl.nut", SQTrue);
    CHECK(SQ_SUCCEEDED(result));
    sq_settop(runtime.handle(), top);
    const script::ScriptMetadata* metadata = runtime.scriptCompiler().metadata("console_repl.nut");
    CHECK(metadata != nullptr);
    CHECK_EQ(metadata->symbols.size(), size_t(1));
    CHECK_EQ(metadata->symbols[0].name, std::string("replValue"));
}

TEST_CASE("scriptCompiler.bindingContractChecksLiteralTypes") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeCount", [](int count) { return count; });
    script::BindingContract contract;
    contract.module     = "test";
    contract.method     = "nativeCount";
    contract.returnType = "int";
    contract.parameters = {{"count", "int", false}};
    runtime.scriptCompiler().bindings().registerContract(std::move(contract));

    bool rejected = false;
    try {
        runtime.compileSource("nativeCount(\"many\")\n", "game:/type-error.nut");
    } catch (const ScriptException& error) {
        rejected = std::string(error.what()).find("not assignable") != std::string::npos;
    }
    CHECK(rejected);
}
