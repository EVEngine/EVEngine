#include "common/Runtime.h"
#include "common/ScriptModule.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <string>
#include <unordered_map>

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

}  // namespace

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
        rejected = std::string(error.what()).find("cyclic script import") != std::string::npos ||
                   runtime.scriptModules().lastError().find("cyclic script import") != std::string::npos;
    }
    CHECK(rejected);
}
