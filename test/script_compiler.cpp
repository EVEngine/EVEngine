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

TEST_CASE("scriptCompiler.metadataIgnoresCommentedLanguageForms") {
    const auto metadata = script::ScriptCompiler::analyze(
        "// import { ghost } from \"game:/missing.nut\"\n"
        "/* export function hidden() {}\n"
        "persist ignored = 1 */\n"
        "local text = \"// await remains text\"\n"
        "import { answer } from \"game:/real.nut\"\n",
        "game:/comments.nut");
    CHECK_EQ(metadata.imports.size(), size_t(1));
    CHECK_EQ(metadata.imports[0], std::string("game:/real.nut"));
    CHECK(metadata.exports.empty());
    CHECK(metadata.persistRoots.empty());
    CHECK(metadata.awaitLocations.empty());
}

TEST_CASE("scriptCompiler.mapsDebuggerLocationsBidirectionally") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.compileSource("local value = 1\n", "game:/scripts/mapped.nut");

    script::ScriptSourceMap map;
    map.entries.push_back({{1, 1}, {1, 1}});
    map.entries.push_back({{10, 1}, {3, 1}});
    REQUIRE(runtime.scriptCompiler().setSourceMap("game:/scripts/mapped.nut", std::move(map)));

    const auto original = script::ScriptCompiler::toOriginalPosition("C:/games/demo/scripts/mapped.nut", {12, 7});
    CHECK_EQ(original.line, uint32_t(5));
    CHECK_EQ(original.column, uint32_t(7));

    const auto generated = script::ScriptCompiler::toGeneratedPosition("game:/scripts/mapped.nut", {5, 7});
    CHECK_EQ(generated.line, uint32_t(12));
    CHECK_EQ(generated.column, uint32_t(7));
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

TEST_CASE("scriptCompiler.nestedBindingCallsKeepOuterSignatureStable") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeInner", [](int value) { return value + 1; });
    runtime.vm().addFunc("nativeOuter", [](int first, int second) { return first * 10 + second; });

    script::BindingContract inner;
    inner.module = "test";
    inner.method = "nativeInner";
    inner.parameters = {{"value", "int"}};
    inner.returnType = "int";
    runtime.scriptCompiler().bindings().registerContract(std::move(inner));

    script::BindingContract outer;
    outer.module = "test";
    outer.method = "nativeOuter";
    outer.parameters = {{"first", "int"}, {"second", "int"}};
    outer.returnType = "int";
    runtime.scriptCompiler().bindings().registerContract(std::move(outer));

    runtime.runSource(
        "nested_binding_result <- nativeOuter(nativeInner(4), 7)\n",
        "nested-binding-signature-test.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("nested_binding_result"), int64_t(57));
}

TEST_CASE("scriptCompiler.generatedContractsDriveSimpleSquirrelNamedArguments") {
    Runtime    runtime(512, ssq::Libs::ALL);
    const auto contracts = runtime.scriptCompiler().bindings().snapshot();
    CHECK(contracts.size() > size_t(100));

    const script::BindingContract* generated =
        runtime.scriptCompiler().bindings().find("animation/AnimRetargetProfile.setNormalizedNameMatching");
    REQUIRE(generated != nullptr);
    REQUIRE_EQ(generated->parameters.size(), size_t(1));
    CHECK_EQ(generated->parameters[0].name, std::string("enabled"));
    CHECK_EQ(generated->parameters[0].type, std::string("bool"));

    runtime.vm().addFunc("setNormalizedNameMatching", [](bool enabled) { return enabled; });
    runtime.runSource("generated_contract_result <- setNormalizedNameMatching(enabled: true)\n",
                      "generated-contract-test.nut");
    CHECK(runtime.vm().get<bool>("generated_contract_result"));
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
    contract.method          = "toolingNewWorld";
    contract.returnType      = "World";
    contract.documentationId = "physics.toolingNewWorld";
    contract.parameters      = {{"gravity", "float"}, {"sleep", "bool"}};
    runtime.scriptCompiler().bindings().registerContract(std::move(contract));

    const auto scriptItems = runtime.scriptCompiler().completions("game:/tooling.nut", "player");
    CHECK_EQ(scriptItems.size(), size_t(1));
    CHECK_EQ(scriptItems[0].label, std::string("playerSpeed"));
    const auto nativeItems = runtime.scriptCompiler().completions("game:/tooling.nut", "toolingNew");
    CHECK_EQ(nativeItems.size(), size_t(1));
    CHECK(nativeItems[0].insertText.find("gravity:") != std::string::npos);

    const auto hover = runtime.scriptCompiler().hover("game:/tooling.nut", "toolingNewWorld");
    REQUIRE(hover.has_value());
    CHECK(hover->markdown.find("physics.toolingNewWorld") != std::string::npos);
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

TEST_CASE("scriptCompiler.bindingContractScopesLiteralChecksToWholeArgument") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.vm().addFunc("nativeText", [](std::string text) { return text; });
    script::BindingContract contract;
    contract.module     = "test";
    contract.method     = "nativeText";
    contract.returnType = "string";
    contract.parameters = {{"text", "string", false}};
    runtime.scriptCompiler().bindings().registerContract(std::move(contract));

    runtime.runSource("compound_contract_result <- nativeText(\"count \" + (1 + 2))\n", "game:/compound-contract.nut");
    CHECK_EQ(runtime.vm().get<std::string>("compound_contract_result"), std::string("count 3"));
}

TEST_CASE("scriptCompiler.validatesConfigModulesWithoutExecution") {
    const auto diagnostics = script::ScriptCompiler::validateProjectConfig(
        "config <- { modules = [\"gfx\", \"physics\", \"typo\"], optionalModules = [\"audio\"] }\n", "game:/config.nut",
        {"gfx", "physics", "audio"}, {"gfx", "audio"});
    CHECK_EQ(diagnostics.size(), size_t(2));
    CHECK_EQ(diagnostics[0].code, std::string("EVE1002"));
    CHECK_EQ(diagnostics[0].related, std::string("physics"));
    CHECK_EQ(diagnostics[1].code, std::string("EVE1001"));
    CHECK_EQ(diagnostics[1].related, std::string("typo"));
}

TEST_CASE("scriptCompiler.compilesRepositoryNutCompatibilityBaseline") {
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS)
    const std::filesystem::path root(EVENGINE_SOURCE_DIR);
    size_t                      compiled = 0;
    for (std::filesystem::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (it->is_directory()) {
            const std::string name = it->path().filename().string();
            if (name == ".git" || name == "build" || name == "third-party") it.disable_recursion_pending();
            continue;
        }
        if (it->path().extension() != ".nut") continue;
        std::ifstream input(it->path(), std::ios::binary);
        REQUIRE(input.good());
        const std::string source(std::istreambuf_iterator<char>(input), {});
        const std::string relative = std::filesystem::relative(it->path(), root).generic_string();
        try {
            Runtime runtime(1024, ssq::Libs::ALL);
            runtime.compileSource(source, "baseline:/" + relative);
        } catch (const std::exception& error) {
            throw std::runtime_error(relative + ": " + error.what());
        }
        ++compiled;
    }
    CHECK(compiled >= size_t(150));
#endif
}
