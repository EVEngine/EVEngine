#include "common/Capability.h"
#include "common/Runtime.h"
#include "common/ScriptCompiler.h"
#include "common/ScriptModule.h"
#include "common/ServiceInterfaces.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace eve;

namespace {

class MemoryModuleProvider final : public script::IScriptModuleProvider {
public:
    std::unordered_map<std::string, std::string> sources;
    std::unordered_map<std::string, int>         loads;

    script::ScriptModuleStatus resolve(const script::ScriptModuleRequest& request, std::string& canonicalUri,
                                       std::string& error) override {
        if (!script::ScriptModuleResolver::canonicalize(request, canonicalUri, error))
            return script::ScriptModuleStatus::Error;
        return canonicalUri.rfind("mem:/", 0) == 0 ? script::ScriptModuleStatus::Found
                                                   : script::ScriptModuleStatus::NotHandled;
    }

    script::ScriptModuleStatus load(std::string_view canonicalUri, script::ScriptModuleSource& source,
                                    std::string& error) override {
        if (canonicalUri.rfind("mem:/", 0) != 0) return script::ScriptModuleStatus::NotHandled;
        const auto found = sources.find(std::string(canonicalUri));
        if (found == sources.end()) {
            error = "missing memory module: " + std::string(canonicalUri);
            return script::ScriptModuleStatus::Error;
        }
        ++loads[found->first];
        source.canonicalUri = found->first;
        source.utf8Source   = found->second;
        source.contentHash  = std::to_string(std::hash<std::string>{}(found->second));
        source.debugOrigin  = "memory-test";
        return script::ScriptModuleStatus::Found;
    }
};

class VirtualFileSystem final : public service::IFileSystem {
public:
    std::unordered_map<std::string, std::string> files;

    bool readFile(const std::string& path, std::vector<uint8_t>& output) override {
        const auto found = files.find(path);
        if (found == files.end()) return false;
        output.assign(found->second.begin(), found->second.end());
        return true;
    }
    bool writeFile(const std::string&, const void*, size_t) override { return false; }
    bool fileExists(const std::string& path) override { return files.find(path) != files.end(); }
};

}  // namespace

TEST_CASE("scriptModule.defaultProviderUsesPlatformVirtualFileSystem") {
    cap::detail::clearAllRaw();
    VirtualFileSystem filesystem;
    filesystem.files["scripts/value.nut"] = "export const VALUE = 42\n";
    cap::provide<service::IFileSystem>(&filesystem);
    {
        Runtime runtime(512, ssq::Libs::ALL);
        runtime.runSource(
            "import { VALUE } from \"game:/scripts/value.nut\"\n"
            "vfs_module_result <- VALUE\n",
            "game:/main.nut");
        CHECK_EQ(runtime.vm().get<int64_t>("vfs_module_result"), int64_t(42));
    }
    cap::detail::clearAllRaw();
}

TEST_CASE("scriptModule.resolvesCachesAndInstantiatesDependencyGraph") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                     = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/values.nut"] = "export const VALUE = 40\n";
    provider->sources["mem:/answer.nut"] =
        "import { VALUE } from \"mem:/values.nut\"\n"
        "export function answer() -> int { return VALUE + 2 }\n";
    runtime.scriptModules().registerProvider(provider, 100);

    runtime.runSource(
        "import { answer } from \"mem:/answer.nut\"\n"
        "import { answer as answer_again } from \"mem:/answer.nut\"\n"
        "module_result <- answer() + answer_again()\n",
        "game:/main.nut");

    const auto moduleResult = runtime.vm().get<int64_t>("module_result");
    CHECK_EQ(moduleResult, int64_t(84));
    CHECK_EQ(provider->loads["mem:/answer.nut"], 1);
    CHECK_EQ(provider->loads["mem:/values.nut"], 1);
    const auto dependencies = runtime.scriptModules().dependencies("game:/main.nut");
    CHECK_EQ(dependencies.size(), size_t(1));
    CHECK_EQ(dependencies[0], std::string("mem:/answer.nut"));
    const auto reverse = runtime.scriptModules().reverseDependencies("mem:/answer.nut");
    CHECK_EQ(reverse.size(), size_t(1));
    CHECK_EQ(reverse[0], std::string("game:/main.nut"));
    const script::ScriptMetadata* metadata = runtime.scriptCompiler().metadata("game:/main.nut");
    CHECK(metadata != nullptr);
    CHECK_EQ(metadata->dependencies, dependencies);
}

TEST_CASE("scriptModule.rejectsCyclesBeforeExecution") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/a.nut"] = "import { b } from \"mem:/b.nut\"\nexport function a() { return b() }\n";
    provider->sources["mem:/b.nut"] = "import { a } from \"mem:/a.nut\"\nexport function b() { return a() }\n";
    runtime.scriptModules().registerProvider(provider, 100);

    bool rejected = false;
    try {
        runtime.compileSource("import { a } from \"mem:/a.nut\"\n", "game:/main.nut");
    } catch (const ScriptException& error) {
        rejected = std::string(error.what()).find("cyclic script import") != std::string::npos;
    }
    REQUIRE(rejected);
}

TEST_CASE("scriptModule.reloadReplacesGenerationAtomically") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                    = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/value.nut"] = "export function value() { return 1 }\n";
    runtime.scriptModules().registerProvider(provider, 100);
    runtime.runSource("import { value } from \"mem:/value.nut\"\nfirst <- value()\n", "game:/first.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("first"), int64_t(1));

    provider->sources["mem:/value.nut"] = "export function value( {\n";
    bool failed                         = false;
    try {
        runtime.scriptModules().reload("mem:/value.nut");
    } catch (const std::exception&) {
        failed = true;
    }
    CHECK(failed);
    runtime.runSource("import { value } from \"mem:/value.nut\"\nold_generation <- value()\n",
                      "game:/old-generation.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("old_generation"), int64_t(1));

    provider->sources["mem:/value.nut"] = "export function value() { return 2 }\n";
    runtime.scriptModules().reload("mem:/value.nut");
    runtime.runSource("import { value } from \"mem:/value.nut\"\nnew_generation <- value()\n",
                      "game:/new-generation.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("new_generation"), int64_t(2));
}

TEST_CASE("scriptModule.reloadAffectedRebuildsReverseDependencyGenerations") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                    = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/value.nut"] = "export function value() { return 1 }\n";
    provider->sources["mem:/user.nut"] =
        "import { value } from \"mem:/value.nut\"\nexport function answer() { return value() }\n";
    runtime.scriptModules().registerProvider(provider, 100);
    runtime.runSource("import { answer } from \"mem:/user.nut\"\nfirst_answer <- answer()\n", "game:/first.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("first_answer"), int64_t(1));

    provider->sources["mem:/value.nut"] = "export function value() { return 2 }\n";
    const auto affected                 = runtime.scriptModules().reloadAffected("mem:/value.nut");
    CHECK_EQ(affected.size(), size_t(2));
    runtime.runSource("import { answer } from \"mem:/user.nut\"\nsecond_answer <- answer()\n", "game:/second.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("second_answer"), int64_t(2));
}

TEST_CASE("scriptModule.reloadAffectedRollsBackWholeGenerationGroup") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                    = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/value.nut"] = "export function value() { return 3 }\n";
    provider->sources["mem:/user.nut"] =
        "import { value } from \"mem:/value.nut\"\nexport function answer() { return value() }\n";
    runtime.scriptModules().registerProvider(provider, 100);
    runtime.runSource("import { answer } from \"mem:/user.nut\"\nbefore_failure <- answer()\n", "game:/before.nut");

    provider->sources["mem:/value.nut"] = "export function value() { return 4 }\n";
    provider->sources["mem:/user.nut"]  = "export function answer( {\n";
    bool failed                         = false;
    try {
        runtime.scriptModules().reloadAffected("mem:/value.nut");
    } catch (const std::exception&) {
        failed = true;
    }
    REQUIRE(failed);
    runtime.runSource("import { answer } from \"mem:/user.nut\"\nafter_failure <- answer()\n", "game:/after.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("after_failure"), int64_t(3));
}
