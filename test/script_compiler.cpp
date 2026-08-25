#include "common/Runtime.h"
#include "common/ScriptCompiler.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

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
