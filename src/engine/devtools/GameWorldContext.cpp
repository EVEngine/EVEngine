#include "devtools/GameWorldContext.hpp"

#include "common/Capability.h"
#include "common/GameplayControl.h"
#include "common/SquirrelBinding.h"
#include "devtools/Debugger.hpp"
#include "devtools/McpServer.hpp"

#include <squirrel.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <utility>

namespace eve::dev {
namespace {

Value g_runtimeWorld;
bool  g_hasRuntimeWorld = false;

Result<void> knownFields(const Value::Object& object, const std::set<std::string>& allowed,
                         std::string_view path) {
    for (const auto& [name, value] : object) {
        (void)value;
        if (!allowed.contains(name))
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "unknown world field",
                                  std::string(path) + "." + name));
    }
    return Result<void>::success();
}

Result<std::vector<std::string>> stringArray(const Value& value, std::string path) {
    const auto* array = value.getIf<Value::Array>();
    if (!array)
        return Result<std::vector<std::string>>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "world array must contain strings", path));
    std::vector<std::string> out;
    out.reserve(array->size());
    for (std::size_t index = 0; index < array->size(); ++index) {
        if (!(*array)[index].isString())
            return Result<std::vector<std::string>>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "world array entry must be a string",
                path + "." + std::to_string(index)));
        out.push_back((*array)[index].asString());
    }
    return Result<std::vector<std::string>>::success(std::move(out));
}

Result<std::string> optionalStringField(const Value::Object& object, std::string_view name,
                                        std::string_view path, std::string* out) {
    const auto found = object.find(std::string(name));
    if (found == object.end()) return Result<std::string>::success(std::string{});
    if (!found->second.isString())
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "world field must be a string",
            std::string(path) + "." + std::string(name)));
    *out = found->second.asString();
    return Result<std::string>::success(*out);
}

Value encodeStrings(const std::vector<std::string>& values) {
    Value::Array array;
    array.reserve(values.size());
    for (const auto& value : values) array.emplace_back(value);
    return Value(std::move(array));
}

std::vector<std::string> uniqueSorted(std::vector<std::string> values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

Value mergeWorldObjects(const Value* contractWorld, const Value* runtimeWorld, std::string* source) {
    const bool hasContract = contractWorld && contractWorld->isObject();
    const bool hasRuntime  = runtimeWorld && runtimeWorld->isObject();
    if (!hasContract && !hasRuntime) {
        *source = "none";
        return Value{};
    }
    if (hasContract && !hasRuntime) {
        *source = "contract";
        return *contractWorld;
    }
    if (!hasContract && hasRuntime) {
        *source = "runtime";
        return *runtimeWorld;
    }
    *source = "merged";
    Value::Object merged = *contractWorld->getIf<Value::Object>();
    for (const auto& [key, value] : *runtimeWorld->getIf<Value::Object>()) {
        if (key == "systems" || key == "genre" || key == "tags") {
            std::vector<std::string> combined;
            if (const auto found = merged.find(key); found != merged.end() && found->second.isArray()) {
                for (const auto& entry : *found->second.getIf<Value::Array>()) {
                    if (entry.isString()) combined.push_back(entry.asString());
                }
            }
            if (value.isArray()) {
                for (const auto& entry : *value.getIf<Value::Array>()) {
                    if (entry.isString()) combined.push_back(entry.asString());
                }
            }
            merged[key] = encodeStrings(uniqueSorted(std::move(combined)));
            continue;
        }
        merged[key] = value;
    }
    return Value(std::move(merged));
}

Result<Value> readRootMember(HSQUIRRELVM vm, const char* name) {
    if (!vm) return Result<Value>::failure(Diagnostic::error(
        DiagnosticCode::NotFound, "no VM attached", "vm"));
    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    sq_pushstring(vm, name, -1);
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, std::string(name) + " missing", name));
    }
    script::SquirrelValueOptions options;
    options.maxDepth    = 8;
    options.maxElements = 4096;
    options.source      = name;
    auto value          = script::valueFromSquirrel(vm, -1, options);
    sq_settop(vm, top);
    return value;
}

void appendStringArrayField(const Value::Object& object, const char* key,
                            std::vector<std::string>* out) {
    const auto found = object.find(key);
    if (found == object.end() || !found->second.isArray()) return;
    for (const auto& entry : *found->second.getIf<Value::Array>()) {
        if (entry.isString() && !entry.asString().empty()) out->push_back(entry.asString());
    }
}

}  // namespace

Result<Value> parseWorldObject(const Value& world) {
    const auto* object = world.getIf<Value::Object>();
    if (!object)
        return Result<Value>::failure(
            Diagnostic::error(DiagnosticCode::ParseError, "world must be an object", "world"));
    auto known = knownFields(*object,
                             {"dimensions", "genre", "perspective", "summary", "systems", "tags", "title"},
                             "world");
    if (!known) return Result<Value>::failure(known.status());

    Value::Object normalized;
    std::string   title;
    std::string   summary;
    std::string   dimensions;
    std::string   perspective;
    auto          titleResult = optionalStringField(*object, "title", "world", &title);
    if (!titleResult) return Result<Value>::failure(titleResult.status());
    auto summaryResult = optionalStringField(*object, "summary", "world", &summary);
    if (!summaryResult) return Result<Value>::failure(summaryResult.status());
    auto dimensionsResult = optionalStringField(*object, "dimensions", "world", &dimensions);
    if (!dimensionsResult) return Result<Value>::failure(dimensionsResult.status());
    auto perspectiveResult = optionalStringField(*object, "perspective", "world", &perspective);
    if (!perspectiveResult) return Result<Value>::failure(perspectiveResult.status());

    if (!dimensions.empty() && dimensions != "2d" && dimensions != "3d" && dimensions != "hybrid")
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "world.dimensions must be 2d, 3d, or hybrid", "world.dimensions"));

    if (!title.empty()) normalized.emplace("title", Value(std::move(title)));
    if (!summary.empty()) normalized.emplace("summary", Value(std::move(summary)));
    if (!dimensions.empty()) normalized.emplace("dimensions", Value(std::move(dimensions)));
    if (!perspective.empty()) normalized.emplace("perspective", Value(std::move(perspective)));

    for (const char* key : {"genre", "tags", "systems"}) {
        const auto found = object->find(key);
        if (found == object->end()) continue;
        auto array = stringArray(found->second, std::string("world.") + key);
        if (!array) return Result<Value>::failure(array.status());
        normalized.emplace(key, encodeStrings(uniqueSorted(std::move(array).takeValue())));
    }

    const bool hasIdentity = normalized.contains("title") || normalized.contains("summary") ||
                             normalized.contains("genre") || normalized.contains("systems");
    if (!hasIdentity)
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::ParseError,
            "world requires at least one of title, summary, genre, or systems", "world"));

    return Result<Value>::success(Value(std::move(normalized)));
}

Result<void> publishWorldContext(Value world) {
    auto parsed = parseWorldObject(world);
    if (!parsed) return Result<void>::failure(parsed.status());
    g_runtimeWorld    = std::move(parsed).takeValue();
    g_hasRuntimeWorld = true;
    return Result<void>::success();
}

void clearWorldContext() {
    g_runtimeWorld    = Value{};
    g_hasRuntimeWorld = false;
}

bool hasWorldContext() noexcept { return g_hasRuntimeWorld; }

const Value* runtimeWorldContext() noexcept {
    return g_hasRuntimeWorld ? &g_runtimeWorld : nullptr;
}

Result<Value> buildGameWorldSnapshot(const GameWorldSnapshotInput& input) {
    const Value* contractWorld = nullptr;
    Value        parsedContract;
    if (input.contractWorld) {
        auto parsed = parseWorldObject(*input.contractWorld);
        if (!parsed) return Result<Value>::failure(parsed.status());
        parsedContract = std::move(parsed).takeValue();
        contractWorld  = &parsedContract;
    }

    std::string source;
    Value       world = mergeWorldObjects(contractWorld, runtimeWorldContext(), &source);

    Value::Object snapshot{{"schemaId", Value(std::string(kGameWorldSchemaId))},
                           {"schemaVersion", Value(kGameWorldSchemaVersion)},
                           {"source", Value(std::move(source))},
                           {"contractLoaded", Value(input.contractLoaded)},
                           {"domains", encodeStrings(uniqueSorted(input.domains))},
                           {"configuredModules", encodeStrings(uniqueSorted(input.configuredModules))},
                           {"liveModules", encodeStrings(uniqueSorted(input.liveModules))}};
    if (!input.contractId.empty()) snapshot.emplace("contractId", Value(input.contractId));
    if (!input.gameRoot.empty()) snapshot.emplace("gameRoot", Value(input.gameRoot));
    if (!world.isNull()) snapshot.emplace("world", std::move(world));
    else snapshot.emplace("world", Value{});
    return Result<Value>::success(Value(std::move(snapshot)));
}

std::vector<std::string> readConfiguredModules(HSQUIRRELVM vm) {
    std::vector<std::string> modules;
    auto                     config = readRootMember(vm, "config");
    if (!config || !config.value().isObject()) {
        if (config) config.ignore();
        return modules;
    }
    const auto* object = config.value().getIf<Value::Object>();
    appendStringArrayField(*object, "modules", &modules);
    appendStringArrayField(*object, "optionalModules", &modules);
    config.ignore();
    return uniqueSorted(std::move(modules));
}

std::vector<std::string> readLiveModules(HSQUIRRELVM vm) {
    std::vector<std::string> live;
    if (!vm) return live;

    const SQInteger top = sq_gettop(vm);
    sq_pushroottable(vm);
    sq_pushstring(vm, _SC("eve"), -1);
    if (SQ_FAILED(sq_get(vm, -2)) || sq_gettype(vm, -1) != OT_TABLE) {
        sq_settop(vm, top);
        return live;
    }
    sq_pushstring(vm, _SC("moduleList"), -1);
    if (SQ_FAILED(sq_get(vm, -2)) || sq_gettype(vm, -1) != OT_ARRAY) {
        sq_settop(vm, top);
        return live;
    }

    const SQInteger count = sq_getsize(vm, -1);
    for (SQInteger index = 0; index < count; ++index) {
        sq_pushinteger(vm, index);
        if (SQ_FAILED(sq_get(vm, -2)) || sq_gettype(vm, -1) != OT_TABLE) {
            if (sq_gettop(vm) > top + 3) sq_poptop(vm);
            continue;
        }
        sq_pushstring(vm, _SC("slot"), -1);
        std::string slotName;
        if (SQ_SUCCEEDED(sq_get(vm, -2))) {
            if (sq_gettype(vm, -1) == OT_STRING) {
                const SQChar* slot = nullptr;
                sq_getstring(vm, -1, &slot);
                if (slot) slotName = slot;
            }
            sq_poptop(vm);  // slot value
        } else {
            sq_poptop(vm);  // unused key
        }
        sq_poptop(vm);  // module entry
        if (slotName.empty()) continue;

        sq_pushroottable(vm);
        sq_pushstring(vm, slotName.c_str(), -1);
        const bool present = SQ_SUCCEEDED(sq_get(vm, -2)) && sq_gettype(vm, -1) == OT_INSTANCE;
        sq_settop(vm, top + 3);  // keep root / eve / moduleList
        if (present) live.push_back(std::move(slotName));
    }
    sq_settop(vm, top);
    return uniqueSorted(std::move(live));
}

Result<Value> liveGameWorldSnapshot() {
    GameWorldSnapshotInput input;
    input.gameRoot          = McpServer::instance().gameRoot();
    input.configuredModules = readConfiguredModules(Debugger::instance().vm());
    input.liveModules       = readLiveModules(Debugger::instance().vm());
    eve::cap::forEach<IGameplayControlProvider>([&](auto* candidate) {
        if (candidate) input.domains.emplace_back(candidate->gameplayDomain());
    });

    std::filesystem::path root = input.gameRoot;
    if (root.empty()) {
        std::error_code ec;
        root = std::filesystem::current_path(ec);
        if (!ec) input.gameRoot = root.string();
    }
    const auto      path = root / "game.agent.json";
    std::error_code ec;
    Value           contractWorld;
    if (std::filesystem::is_regular_file(path, ec)) {
        std::ifstream in(path, std::ios::binary);
        if (in) {
            std::ostringstream contents;
            contents << in.rdbuf();
            auto parsed = Value::fromJson(contents.str());
            if (!parsed) return Result<Value>::failure(parsed.status());
            if (parsed.value().isObject()) {
                input.contractLoaded = true;
                const auto* object   = parsed.value().getIf<Value::Object>();
                const auto  id       = object->find("id");
                if (id != object->end() && id->second.isString()) input.contractId = id->second.asString();
                const auto world = object->find("world");
                if (world != object->end()) {
                    auto normalized = parseWorldObject(world->second);
                    if (!normalized) return Result<Value>::failure(normalized.status());
                    contractWorld       = std::move(normalized).takeValue();
                    input.contractWorld = &contractWorld;
                }
            }
        }
    }
    return buildGameWorldSnapshot(input);
}

}  // namespace eve::dev
