#include "common/BindingContracts.h"
#include "common/Runtime.h"
#include "common/ScriptCompiler.h"
#include "common/ScriptModule.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

using namespace eve;


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

TEST_CASE("scriptCompiler.bindingContractSearchRanksExactMethod") {
    script::BindingContractRegistry registry;
    script::BindingContract         camera;
    camera.module      = "camera";
    camera.scriptClass = "CameraController";
    camera.method      = "setYaw";
    camera.parameters.push_back({"yaw", "float", false, std::nullopt, script::ScriptUnit::Radians, {}});
    camera.returnType      = "void";
    camera.documentationId = "camera.CameraController.setYaw";
    registry.registerContract(camera);

    script::BindingContract other;
    other.module      = "camera";
    other.scriptClass = "CameraController";
    other.method      = "setPitch";
    other.returnType  = "void";
    registry.registerContract(other);

    const auto hits = registry.search("setYaw");
    REQUIRE(hits.size() == size_t(1));
    CHECK_EQ(hits[0].contract.method, std::string("setYaw"));
    CHECK(hits[0].score > 100);
    CHECK(hits[0].contract.toJson().find("\"unit\":\"Radians\"") != std::string::npos);

    const auto filtered = registry.search("set", "camera", "CameraController", 8);
    CHECK_EQ(filtered.size(), size_t(2));
}

TEST_CASE("scriptCompiler.generatedBindingContractsAreSearchable") {
    const auto hits = script::generatedBindingContracts().search("newWorld", {}, {}, 8);
    CHECK(!hits.empty());
    CHECK(hits[0].contract.toJson().find("\"method\":\"") != std::string::npos);
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


TEST_CASE("scriptCompiler.rawVmBridgeRecordsToolingMetadata") {
    Runtime         runtime(512, ssq::Libs::ALL);
    const char*     source = "local replValue: int = 7\n";
    const SQInteger top    = sq_gettop(runtime.handle());
    const SQRESULT  result = script::ScriptCompiler::compileBuffer(
        runtime.handle(), source, static_cast<SQInteger>(std::strlen(source)), "console_repl.nut", SQTrue);
    CHECK(SQ_SUCCEEDED(result));
    sq_settop(runtime.handle(), top);
    const auto metadata = runtime.scriptCompiler().metadata("console_repl.nut");
    REQUIRE(metadata.has_value());
    CHECK_EQ(metadata->get().symbols.size(), size_t(1));
    CHECK_EQ(metadata->get().symbols[0].name, std::string("replValue"));
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
