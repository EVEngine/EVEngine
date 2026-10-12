#include <Poco/JSON/Array.h>
#include <Poco/JSON/Parser.h>
#include <algorithm>
#include <stdexcept>
#include "common/BindingContracts.h"
#include "common/ScriptCompiler.h"
#include "devtools/McpJson.hpp"
#include "devtools/ProjectIndexInternal.hpp"

namespace eve::dev::project_index {
namespace {
using Array = Poco::JSON::Array;
struct Match {
    std::string path, pointer, hash;
    unsigned    line = 0;
    Object::Ptr value;
    std::string body;
};
void diagnostic(Snapshot& snapshot, const std::string& code, const std::string& id, const std::string& message) {
    Object::Ptr out = new Object();
    out->set("code", code);
    out->set("id", id);
    out->set("message", message);
    snapshot.diagnostics.push_back(out);
}
std::string valueType(const Poco::Dynamic::Var& value) {
    if (value.isEmpty()) return "null";
    if (value.type() == typeid(Object::Ptr)) return "object";
    if (value.type() == typeid(Array::Ptr)) return "array";
    if (value.isString()) return "string";
    if (value.isBoolean()) return "boolean";
    if (value.isInteger()) return "integer";
    return value.isNumeric() ? "number" : "unknown";
}
std::string escapedPointer(const std::string& name) {
    std::string out;
    for (const char c : name) out += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return out;
}
Object::Ptr source(const Match& match) {
    Object::Ptr out = new Object();
    out->set("file", match.path);
    out->set("pointer", match.pointer);
    if (match.line) out->set("line", match.line);
    out->set("inputHash", match.hash);
    return out;
}
Object::Ptr node(const std::string& id, const std::string& kind, const std::string& label) {
    Object::Ptr out = new Object();
    out->set("id", id);
    out->set("kind", kind);
    out->set("label", label);
    out->set("status", "resolved");
    return out;
}
void addEdge(Snapshot& snapshot, const std::string& from, const std::string& to, const std::string& relation,
             const std::string& evidence, Object::Ptr origin = {}) {
    Object::Ptr out = new Object();
    out->set("from", from);
    out->set("to", to);
    out->set("relation", relation);
    out->set("evidence", evidence);
    if (origin) out->set("source", origin);
    snapshot.edges.push_back(out);
}
void flatten(Snapshot& snapshot, std::vector<Match>& objects, const Poco::Dynamic::Var& value, const Match& location,
             const std::string& parent = {}) {
    if (snapshot.nodes.size() >= 16384) throw std::runtime_error("Project structure budget exceeded");
    const auto id   = "json:" + location.path + "#" + location.pointer;
    auto       item = node(id, "json_" + valueType(value), location.pointer.empty() ? location.path : location.pointer);
    item->set("source", source(location));
    item->set("evidence", "parsed-json");
    snapshot.nodes.emplace(id, item);
    if (!parent.empty()) addEdge(snapshot, parent, id, "contains", "parsed-json");
    if (value.type() == typeid(Object::Ptr)) {
        auto object = value.extract<Object::Ptr>();
        auto match  = location;
        match.value = object;
        objects.push_back(match);
        std::vector<std::string> names;
        object->getNames(names);
        Array::Ptr fields = new Array();
        for (const auto& name : names) {
            Object::Ptr field = new Object();
            field->set("name", name);
            field->set("type", valueType(object->get(name)));
            fields->add(field);
            auto child = location;
            child.pointer += "/" + escapedPointer(name);
            flatten(snapshot, objects, object->get(name), child, id);
        }
        item->set("fields", fields);
    } else if (value.type() == typeid(Array::Ptr)) {
        auto values = value.extract<Array::Ptr>();
        item->set("count", values->size());
        for (size_t index = 0; index < values->size(); ++index) {
            auto child = location;
            child.pointer += "/" + std::to_string(index);
            flatten(snapshot, objects, values->get(static_cast<unsigned>(index)), child, id);
        }
    } else if (value.isString()) {
        const auto text = value.convert<std::string>();
        item->set("value", text.substr(0, 512));
        item->set("valueTruncated", text.size() > 512);
    } else
        item->set("value", value);
}
std::vector<Match> resolve(Object::Ptr declaration, const std::vector<Match>& objects,
                           const std::map<std::string, std::vector<Match>>& functions) {
    std::vector<Match> matches;
    if (static_cast<int>(declaration->has("symbol")) + static_cast<int>(declaration->has("jsonId")) != 1)
        throw std::runtime_error("Source declaration requires exactly one symbol or jsonId selector");
    if (declaration->has("symbol")) {
        const auto symbol = requiredString(declaration, "symbol");
        const auto found  = functions.find(symbol);
        if (found != functions.end()) matches = found->second;
        if (declaration->has("contains")) {
            const auto anchor = requiredString(declaration, "contains");
            std::erase_if(matches, [&](const Match& match) { return match.body.find(anchor) == std::string::npos; });
        }
    } else if (declaration->has("jsonId")) {
        const auto id = requiredString(declaration, "jsonId");
        for (const auto& match : objects) {
            if (!match.value->has("id") || !match.value->get("id").isString() ||
                match.value->getValue<std::string>("id") != id)
                continue;
            bool valid = true;
            if (declaration->has("fields")) {
                auto fields = declaration->getArray("fields");
                if (!fields) throw std::runtime_error("fields must be an array");
                for (size_t index = 0; index < fields->size(); ++index) {
                    const auto field = fields->get(static_cast<unsigned>(index));
                    if (!field.isString()) throw std::runtime_error("field name must be a string");
                    if (!match.value->has(field.convert<std::string>())) valid = false;
                }
            }
            if (valid) matches.push_back(match);
        }
    } else
        throw std::runtime_error("Semantic node requires api, symbol or jsonId");
    return matches;
}
}  // namespace
Snapshot project(Files files, Object::Ptr stats, std::string defaultProjectId) {
    Snapshot out;
    out.files                = std::move(files);
    out.stats                = stats;
    const auto  manifestFile = out.files.find("game.semantic.json");
    Object::Ptr manifest;
    if (manifestFile == out.files.end()) {
        manifest = new Object();
        manifest->set("schema", "eve.project-semantic");
        manifest->set("schemaVersion", 2);
        manifest->set("projectId", defaultProjectId);
    } else {
        if (!manifestFile->second->error.empty() || manifestFile->second->json.type() != typeid(Object::Ptr))
            throw std::runtime_error("Missing or invalid game.semantic.json");
        manifest = manifestFile->second->json.extract<Object::Ptr>();
    }
    if (requiredString(manifest, "schema") != "eve.project-semantic" || !manifest->has("schemaVersion") ||
        !manifest->get("schemaVersion").isInteger() ||
        (manifest->getValue<int>("schemaVersion") != 1 && manifest->getValue<int>("schemaVersion") != 2))
        throw std::runtime_error("Unsupported project semantic schema");
    out.projectId                                     = requiredString(manifest, "projectId");
    std::string                               version = out.projectId + '\0';
    std::vector<Match>                        objects;
    std::map<std::string, std::vector<Match>> functions;
    for (const auto& [path, file] : out.files) {
        version += path + '\0' + file->hash + '\0';
        auto item = node("file:" + path, "file", path);
        item->set("inputHash", file->hash);
        item->set("parser", std::filesystem::path(path).extension() == ".dnut" ? "opaque-dsl" : "source-metadata");
        out.nodes.emplace("file:" + path, item);
        if (!file->error.empty()) {
            diagnostic(out, "parse_error", path, file->error);
            continue;
        }
        if (!file->json.isEmpty())
            flatten(out, objects, file->json, Match{path, "", file->hash, 0, {}, {}}, "file:" + path);
        for (const auto& function : file->functions) {
            Match match{path, "", file->hash, function.line, {}, function.body};
            functions[function.name].push_back(match);
            const auto id     = "symbol:" + path + ":" + function.name + ":" + std::to_string(function.line);
            auto       symbol = node(id, "script_symbol", function.name);
            symbol->set("source", source(match));
            symbol->set("erasedType", "dynamic");
            symbol->set("evidence", "source-metadata");
            out.nodes.emplace(id, symbol);
            addEdge(out, "file:" + path, id, "declares", "source-metadata");
        }
        for (const auto& reference : file->references) {
            const auto target = pathText(sourcePath(reference).lexically_normal());
            if (sourcePath(reference).is_absolute() || target.starts_with("../") || !out.files.contains(target))
                diagnostic(out, "missing_script_reference", path, reference);
            else
                addEdge(out, "file:" + path, "file:" + target, "loads", "literal-dofile-reference");
        }
    }
    if (manifest->getValue<int>("schemaVersion") == 2) {
        if (manifest->has("nodes") || manifest->has("links"))
            throw std::runtime_error("Compiler-derived projects do not accept manual nodes or links");
        out.automatic = true;
        automaticGraph(out, version);
        out.revision = digest(version);
        return out;
    }
    auto declarations = manifest->getArray("nodes"), links = manifest->getArray("links");
    if (!declarations || !links || declarations->size() > 256 || links->size() > 1024)
        throw std::runtime_error("Invalid semantic nodes/links budget");
    for (size_t index = 0; index < declarations->size(); ++index) {
        auto       declaration = declarations->getObject(static_cast<unsigned>(index));
        const auto id = requiredString(declaration, "id"), kind = requiredString(declaration, "kind");
        if (static_cast<int>(declaration->has("api")) + static_cast<int>(declaration->has("symbol")) +
                static_cast<int>(declaration->has("jsonId")) !=
            1)
            throw std::runtime_error("Semantic node requires exactly one api, symbol or jsonId selector");
        if (out.nodes.contains(id)) throw std::runtime_error("Duplicate semantic node ID: " + id);
        auto item = node(id, kind, id);
        item->set("evidence", "project-declaration");
        if (declaration->has("api")) {
            const auto  key      = requiredString(declaration, "api");
            const auto* contract = eve::script::generatedBindingContracts().find(key);
            version += key + '\0' + (contract ? contract->toJson() : "unavailable") + '\0';
            if (contract) {
                Poco::JSON::Parser parser;
                item->set("contract", parser.parse(contract->toJson()));
                item->set("evidence", "engine-build-contract");
            } else
                item->set("status", "unavailable");
        } else {
            const auto matches = resolve(declaration, objects, functions);
            if (matches.size() == 1) {
                item->set("source", source(matches.front()));
                if (matches.front().value) item->set("definition", matches.front().value);
                if (!matches.front().body.empty()) {
                    item->set("snippet", matches.front().body.substr(0, 2048));
                    item->set("snippetTruncated", matches.front().body.size() > 2048);
                }
            } else
                item->set("status", matches.empty() ? "missing" : "ambiguous");
        }
        if (item->getValue<std::string>("status") != "resolved")
            diagnostic(out, "unresolved_semantic_node", id, item->getValue<std::string>("status"));
        out.nodes.emplace(id, item);
    }
    for (size_t index = 0; index < links->size(); ++index) {
        auto       link = links->getObject(static_cast<unsigned>(index));
        const auto from = requiredString(link, "from"), to = requiredString(link, "to");
        const auto relation = requiredString(link, "relation");
        const auto matches  = resolve(link, objects, functions);
        if (!out.nodes.contains(from) || !out.nodes.contains(to) || matches.size() != 1 ||
            out.nodes.at(from)->getValue<std::string>("status") != "resolved" ||
            out.nodes.at(to)->getValue<std::string>("status") != "resolved") {
            diagnostic(out, "unresolved_semantic_link", from + " -> " + to, relation);
            continue;
        }
        addEdge(out, from, to, relation, "checked-project-declaration", source(matches.front()));
    }
    out.revision = digest(version);
    return out;
}
}  // namespace eve::dev::project_index
