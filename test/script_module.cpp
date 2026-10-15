#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/Capability.h"
#include "common/Runtime.h"
#include "common/ScriptCompiler.h"
#include "common/ScriptModule.h"
#include "common/ServiceInterfaces.h"
#include "filesystem/physfs/FileApi.h"
#include "property_access/PropertyAccess.h"
#include "property_access/squirrel/ReflectedPropertyModel.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// script_module.cpp
using namespace eve;

namespace {

class MemoryModuleProvider final : public script::IScriptModuleProvider {
public:
    std::unordered_map<std::string, std::string> sources;
    std::unordered_map<std::string, int>         loads;

    script::ScriptModuleStatus resolve(const script::ScriptModuleRequest &request, std::string &canonicalUri,
                                       std::string &error) override {
        if (!script::ScriptModuleResolver::canonicalize(request, canonicalUri, error))
            return script::ScriptModuleStatus::Error;
        return canonicalUri.rfind("mem:/", 0) == 0 ? script::ScriptModuleStatus::Found
                                                   : script::ScriptModuleStatus::NotHandled;
    }

    script::ScriptModuleStatus load(std::string_view canonicalUri, script::ScriptModuleSource &source,
                                    std::string &error) override {
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

    bool readFile(const std::string &path, std::vector<uint8_t> &output) override {
        const auto found = files.find(path);
        if (found == files.end()) return false;
        output.assign(found->second.begin(), found->second.end());
        return true;
    }
    bool writeFile(const std::string &, const void *, size_t) override { return false; }
    bool fileExists(const std::string &path) override { return files.find(path) != files.end(); }
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
    const auto metadata = runtime.scriptCompiler().metadata("game:/main.nut");
    REQUIRE(metadata.has_value());
    CHECK_EQ(metadata->get().dependencies, dependencies);
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
    } catch (const ScriptException &error) {
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
    } catch (const std::exception &) {
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
    } catch (const std::exception &) {
        failed = true;
    }
    REQUIRE(failed);
    runtime.runSource("import { answer } from \"mem:/user.nut\"\nafter_failure <- answer()\n", "game:/after.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("after_failure"), int64_t(3));
}

TEST_CASE("scriptModule.instantiationFailureCarriesTheScriptError") {
    Runtime runtime(512, ssq::Libs::ALL);
    auto    provider                     = std::make_shared<MemoryModuleProvider>();
    provider->sources["mem:/broken.nut"] = "export const VALUE = 1\nthrow \"module body exploded\"\n";
    runtime.scriptModules().registerProvider(provider, 100);

    std::string message;
    try {
        runtime.runSource("import { VALUE } from \"mem:/broken.nut\"\n", "game:/main.nut");
    } catch (const std::exception& error) {
        message = error.what();
    }

    // The module body executes from native code, and its failure used to be thrown away:
    // the caller got "failed to instantiate script module: mem:/broken.nut" and nothing
    // about the throw. The reported text now travels with the exception. Both assertions
    // are REQUIRE: as CHECK they were warning-level and this test passed with the report
    // removed.
    REQUIRE(!message.empty());
    REQUIRE(message.find("failed to instantiate script module") != std::string::npos);
    REQUIRE(message.find("module body exploded") != std::string::npos);
}

TEST_CASE("scriptModule.dofileInstantiatesImports") {
    cap::detail::clearAllRaw();
    VirtualFileSystem filesystem;
    filesystem.files["lib/value.nut"] = "export const VALUE = 41\n";
    cap::provide<service::IFileSystem>(&filesystem);

    const auto root =
        std::filesystem::temp_directory_path() /
        ("eve_dofile_import_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto entry = root / "entry.nut";
    {
        std::ofstream out(entry, std::ios::binary);
        out << "import { VALUE } from \"game:/lib/value.nut\"\n"
               "imported_from_dofile <- VALUE + 1\n";
    }

    Runtime runtime(512, ssq::Libs::ALL);
    eve::filesystem::physfs::installScriptFileApi(runtime.vm());
    const std::string loader = "dofile(\"" + entry.generic_string() + "\")\n";
    runtime.runSource(loader, "game:/loader.nut");
    CHECK_EQ(runtime.vm().get<int64_t>("imported_from_dofile"), int64_t(42));

    std::filesystem::remove_all(root);
    cap::detail::clearAllRaw();
}

// script_property_model.cpp
#include "common/Runtime.h"
#include "property_access/PropertyAccess.h"
#include "property_access/squirrel/ReflectedPropertyModel.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace eve;
using namespace eve::property_access;

namespace {

const char *kModelScript = R"SQ(
class ModelBase {
    baseValue = 2
}
class ModelHero extends ModelBase {
    </ editor = "slider", min = 0, max = 100, step = 1,
       label = "Health", tooltip = "Current health" />
    hp = 80.0
    </ editor = "combo", options = "warrior,mage" />
    job = "warrior"
    alive = true
    tags = ["player", "hero"]
    stats = { armor = 3 }
    </ editor = "color" />
    tint = [1.0, 0.5, 0.25, 1.0]
    </ editor = "vec2" />
    size = [2.0, 3.0]
}
)SQ";

}  // namespace

TEST_CASE("property_access.reflection_builds_shared_schema_and_structured_values") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "property_access.nut");
    ssq::Object hero = runtime.createInstance("ModelHero");

    ReflectedPropertyModel model(runtime, hero);
    CHECK_EQ(model.schema().typeId, std::string("ModelHero"));

    auto hpRef = model.schema().find("hp");
    REQUIRE(hpRef.has_value());
    const PropertyDescriptor *hp = &hpRef->get();
    CHECK(static_cast<int>(hp->kind) == static_cast<int>(PropertyKind::Number));
    CHECK_EQ(hp->displayName, std::string("Health"));
    CHECK_EQ(hp->description, std::string("Current health"));
    CHECK_EQ(hp->category, std::string("ModelHero"));
    REQUIRE(hp->numeric.minimum.has_value());
    REQUIRE(hp->numeric.maximum.has_value());
    CHECK_EQ(*hp->numeric.minimum, 0.0);
    CHECK_EQ(*hp->numeric.maximum, 100.0);

    auto baseRef = model.schema().find("baseValue");
    REQUIRE(baseRef.has_value());
    const PropertyDescriptor *base = &baseRef->get();
    CHECK_EQ(base->category, std::string("ModelBase"));

    auto jobRef = model.schema().find("job");
    REQUIRE(jobRef.has_value());
    const PropertyDescriptor *job = &jobRef->get();
    CHECK(static_cast<int>(job->kind) == static_cast<int>(PropertyKind::Enum));
    CHECK(job->choices == std::vector<std::string>({"warrior", "mage"}));

    auto tagsRef = model.schema().find("tags");
    REQUIRE(tagsRef.has_value());
    const PropertyDescriptor *tags = &tagsRef->get();
    CHECK(!hasFlag(tags->flags, PropertyFlag::ReadOnly));
    CHECK(static_cast<int>(tags->kind) == static_cast<int>(PropertyKind::Array));
    const std::optional<Value> tagValue = model.read("tags");
    REQUIRE(tagValue.has_value());
    const Value::Array *tagArray = tagValue->getIf<Value::Array>();
    REQUIRE(tagArray != nullptr);
    CHECK_EQ(tagArray->size(), static_cast<std::size_t>(2));

    const std::optional<Value> statsValue = model.read("stats");
    REQUIRE(statsValue.has_value());
    const Value::Object *stats = statsValue->getIf<Value::Object>();
    REQUIRE(stats != nullptr);
    CHECK(stats->contains("armor"));

    auto tintRef = model.schema().find("tint");
    REQUIRE(tintRef.has_value());
    CHECK(static_cast<int>(tintRef->get().kind) == static_cast<int>(PropertyKind::Color));
    CHECK(!hasFlag(tintRef->get().flags, PropertyFlag::ReadOnly));

    auto sizeRef = model.schema().find("size");
    REQUIRE(sizeRef.has_value());
    CHECK(static_cast<int>(sizeRef->get().kind) == static_cast<int>(PropertyKind::Vec2));
}

TEST_CASE("property_access.writes_and_refreshes_through_shared_mvvm_contract") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "property_access.nut");
    ssq::Object            hero = runtime.createInstance("ModelHero");
    ReflectedPropertyModel model(runtime, hero);

    std::vector<PropertyChange> changes;
    auto subscription = model.subscribe([&](const PropertyChange &change) { changes.push_back(change); });

    CHECK(model.write("hp", Value(42.0)).ok());
    CHECK_EQ(runtime.readProperty(hero, "hp").asFloat(), 42.0);
    CHECK(!changes.empty());
    CHECK_EQ(changes.back().path, std::string("hp"));

    CHECK(!model.write("hp", Value(101.0)).ok());
    CHECK(!model.write("hp", Value("fast")).ok());
    CHECK_EQ(runtime.readProperty(hero, "hp").asFloat(), 42.0);

    Value::Array tags = {Value(std::string("player")), Value(std::string("elite"))};
    CHECK(model.write("tags", Value(tags)).ok());
    CHECK_EQ(runtime.arraySize(hero, "tags"), static_cast<std::size_t>(2));
    CHECK_EQ(runtime.arrayGet(hero, "tags", 1).asString(), std::string("elite"));

    Value::Array tint = {Value(0.1), Value(0.2), Value(0.3), Value(0.4)};
    CHECK(model.write("tint", Value(tint)).ok());
    CHECK(std::fabs(runtime.arrayGet(hero, "tint", 2).asFloat() - 0.3f) < 1e-5f);

    Value::Array badTint = {Value(0.1), Value(0.2)};
    CHECK(!model.write("tint", Value(badTint)).ok());

    Value::Object stats;
    stats.emplace("armor", Value(std::int64_t(9)));
    stats.emplace("resist", Value(1.5));
    CHECK(model.write("stats", Value(stats)).ok());
    CHECK_EQ(runtime.tableGet(hero, "stats", "armor").asInt(), static_cast<std::int64_t>(9));
    CHECK_EQ(runtime.tableGet(hero, "stats", "resist").asFloat(), 1.5);

    ReflectedValue external;
    external.kind    = ReflectedValueKind::Bool;
    external.boolean = false;
    REQUIRE(runtime.writeProperty(hero, "alive", external));
    const std::size_t before = changes.size();
    model.refresh();
    REQUIRE(changes.size() > before);
    CHECK_EQ(changes.back().path, std::string("alive"));
    const bool *alive = changes.back().value.getIf<bool>();
    REQUIRE(alive != nullptr);
    CHECK(!*alive);
}

TEST_CASE("property_access.script_validation_matches_shared_contract") {
    Runtime runtime(512, ssq::Libs::ALL);
    runtime.initialize();
    runtime.runSource(kModelScript, "property_access.nut");
    ssq::Object            hero = runtime.createInstance("ModelHero");
    ReflectedPropertyModel model(runtime, hero);

    auto hpRef   = model.schema().find("hp");
    auto jobRef  = model.schema().find("job");
    auto tintRef = model.schema().find("tint");
    REQUIRE(hpRef.has_value());
    REQUIRE(jobRef.has_value());
    REQUIRE(tintRef.has_value());
    const PropertyDescriptor *hp   = &hpRef->get();
    const PropertyDescriptor *job  = &jobRef->get();
    const PropertyDescriptor *tint = &tintRef->get();

    const auto sharedType  = validatePropertyValue(*hp, Value("fast"));
    const auto runtimeType = model.write("hp", Value("fast"));
    CHECK(!sharedType.ok());
    CHECK(!runtimeType.ok());
    CHECK_EQ(writeRule(sharedType), std::string("property_access.property.type"));
    CHECK_EQ(writeRule(runtimeType), std::string("property_access.script.type"));

    const auto sharedFinite  = validatePropertyValue(*hp, Value(std::numeric_limits<double>::quiet_NaN()));
    const auto runtimeFinite = model.write("hp", Value(std::numeric_limits<double>::quiet_NaN()));
    CHECK(!sharedFinite.ok());
    CHECK(!runtimeFinite.ok());
    CHECK_EQ(writeRule(sharedFinite), std::string("property_access.property.finite"));
    CHECK_EQ(writeRule(runtimeFinite), std::string("property_access.script.finite"));

    const auto sharedMinimum  = validatePropertyValue(*hp, Value(-1.0));
    const auto runtimeMinimum = model.write("hp", Value(-1.0));
    CHECK(!sharedMinimum.ok());
    CHECK(!runtimeMinimum.ok());
    CHECK_EQ(writeRule(sharedMinimum), std::string("property_access.property.minimum"));
    CHECK_EQ(writeRule(runtimeMinimum), std::string("property_access.script.minimum"));

    const auto sharedMaximum  = validatePropertyValue(*hp, Value(101.0));
    const auto runtimeMaximum = model.write("hp", Value(101.0));
    CHECK(!sharedMaximum.ok());
    CHECK(!runtimeMaximum.ok());
    CHECK_EQ(writeRule(sharedMaximum), std::string("property_access.property.maximum"));
    CHECK_EQ(writeRule(runtimeMaximum), std::string("property_access.script.maximum"));

    const auto sharedChoice  = validatePropertyValue(*job, Value("rogue"));
    const auto runtimeChoice = model.write("job", Value("rogue"));
    CHECK(!sharedChoice.ok());
    CHECK(!runtimeChoice.ok());
    CHECK_EQ(writeRule(sharedChoice), std::string("property_access.property.choice"));
    CHECK_EQ(writeRule(runtimeChoice), std::string("property_access.script.choice"));

    Value::Array badArity     = {Value(1.0), Value(2.0)};
    const auto   sharedArity  = validatePropertyValue(*tint, Value(badArity));
    const auto   runtimeArity = model.write("tint", Value(badArity));
    CHECK(!sharedArity.ok());
    CHECK(!runtimeArity.ok());
    CHECK_EQ(writeRule(sharedArity), std::string("property_access.property.arity"));
    CHECK_EQ(writeRule(runtimeArity), std::string("property_access.script.arity"));
}
