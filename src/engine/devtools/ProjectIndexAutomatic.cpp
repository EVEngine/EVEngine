#include <Poco/JSON/Array.h>
#include <Poco/JSON/Parser.h>
#include <algorithm>
#include <set>
#include "common/BindingContracts.h"
#include "common/ScriptCompiler.h"
#include "devtools/ProjectIndexInternal.hpp"

namespace eve::dev::project_index {
namespace {
using Expression = eve::script::ScriptExpression;
using Contract   = eve::script::BindingContract;
using Array      = Poco::JSON::Array;
struct Type {
    std::string           name;
    std::set<std::string> values, identities, hosts;
};
struct Function {
    std::string                              file;
    const eve::script::ScriptFunctionSyntax* syntax;
};
struct Analysis {
    Snapshot&                                    snapshot;
    const eve::script::BindingContractRegistry&  registry;
    std::map<std::string, std::vector<Function>> functions;
    std::map<std::string, std::string>           factories;
    std::map<std::string, Type>                  variables;
    std::map<std::string, Object::Ptr>           edgeKeys;
    std::map<std::string, const Contract*>       methods;

    std::string localKey(const std::string& file, const std::string& scope, const std::string& name) const {
        return file + "#" + scope + "#" + name;
    }
    std::string bindingKey(const std::string& file, const eve::script::ScriptBindingSyntax& binding) const {
        return binding.local ? localKey(file, binding.scope, binding.name) : "global:" + binding.name;
    }
    Type variable(const std::string& file, const std::string& scope, const std::string& name) const {
        auto local = variables.find(localKey(file, scope, name));
        if (local != variables.end()) return local->second;
        auto global = variables.find("global:" + name);
        return global == variables.end() ? Type{} : global->second;
    }
    bool merge(Type& into, const Type& from) {
        if (from.name.empty() || from.name == "null") return false;
        auto before = into;
        if (into.name.empty() || into.name == "null")
            into.name = from.name;
        else if (into.name != from.name)
            into.name = "dynamic";
        into.values.insert(from.values.begin(), from.values.end());
        into.identities.insert(from.identities.begin(), from.identities.end());
        into.hosts.insert(from.hosts.begin(), from.hosts.end());
        if (into.values.size() > 8 || into.identities.size() > 8 || into.hosts.size() > 8) {
            into.name = "dynamic";
            into.values.clear();
            into.identities.clear();
            into.hosts.clear();
        }
        return before.name != into.name || before.values != into.values || before.identities != into.identities ||
               before.hosts != into.hosts;
    }
    const Contract* contract(const Expression& expression, const std::string& file, const std::string& scope,
                             unsigned depth = 0) const {
        if (depth >= 64 || expression.kind != "call" || expression.operands.empty()) return nullptr;
        const auto& callee = expression.operands[0];
        if (callee.kind != "member" || callee.operands.empty()) return nullptr;
        auto owner = type(callee.operands[0], file, scope, depth + 1);
        if (owner.name.starts_with("module:")) return registry.find(owner.name.substr(7) + "/" + callee.value);
        auto found = methods.find(owner.name + '\0' + callee.value);
        return found == methods.end() ? nullptr : found->second;
    }
    Type type(const Expression& expression, const std::string& file, const std::string& scope,
              unsigned depth = 0) const {
        if (depth >= 64) return {};
        if (expression.kind == "string") return {"string", {expression.value}, {}, {}};
        if (expression.kind == "number") return {"number", {}, {}, {}};
        if (expression.kind == "identifier") {
            if (expression.value == "null") return {"null", {}, {}, {}};
            return variable(file, scope, expression.value);
        }
        if (expression.kind != "call" || expression.operands.empty()) return {};
        const auto& callee = expression.operands[0];
        if (callee.kind == "member" && !callee.operands.empty() && callee.operands[0].kind == "identifier" &&
            callee.operands[0].value == "eve" && !variables.contains(localKey(file, scope, "eve")) &&
            !variables.contains("global:eve")) {
            auto factory = factories.find(callee.value);
            if (factory != factories.end() && expression.operands.size() == 1)
                return {"module:" + factory->second, {}, {}, {}};
        }
        const auto* api = contract(expression, file, scope, depth + 1);
        if (!api) return {};
        Type value;
        value.name =
            api->semantics && !api->semantics->resultClass.empty() ? api->semantics->resultClass : api->returnType;
        if (value.name == "dynamic") value.name.clear();
        if (api->semantics && api->semantics->role == "scene_ref") {
            value.identities = argument(expression, api->semantics->identityArgument, file, scope).values;
            value.hosts      = argument(expression, api->semantics->relatedArgument, file, scope).values;
        }
        return value;
    }
    Type argument(const Expression& call, int index, const std::string& file, const std::string& scope) const {
        if (index < 0 || static_cast<size_t>(index + 1) >= call.operands.size()) return {};
        return type(call.operands[static_cast<size_t>(index + 1)], file, scope);
    }
    Object::Ptr source(const std::string& file, unsigned line, const std::string& pointer = "") const {
        Object::Ptr value = new Object();
        value->set("file", file);
        value->set("inputHash", snapshot.files.at(file)->hash);
        if (line) value->set("line", line);
        if (!pointer.empty()) value->set("pointer", pointer);
        return value;
    }
    Object::Ptr node(const std::string& id, const std::string& kind, Object::Ptr origin = {}) {
        auto found = snapshot.nodes.find(id);
        if (found != snapshot.nodes.end()) return found->second;
        if (snapshot.nodes.size() >= 16384) throw std::runtime_error("Automatic graph node budget exceeded");
        Object::Ptr value = new Object();
        value->set("id", id);
        value->set("kind", kind);
        value->set("label", id);
        value->set("status", "resolved");
        value->set("evidence", "compiler-derived");
        value->set("semantic", true);
        if (origin) value->set("source", origin);
        snapshot.nodes.emplace(id, value);
        return value;
    }
    void edge(const std::string& from, const std::string& to, const std::string& relation, Object::Ptr origin) {
        if (!snapshot.nodes.contains(from) || !snapshot.nodes.contains(to) ||
            snapshot.nodes.at(from)->getValue<std::string>("status") != "resolved" ||
            snapshot.nodes.at(to)->getValue<std::string>("status") != "resolved")
            return;
        const auto key = from + '\0' + to + '\0' + relation;
        if (auto found = edgeKeys.find(key); found != edgeKeys.end()) {
            found->second->set("occurrences", found->second->getValue<unsigned>("occurrences") + 1);
            return;
        }
        if (snapshot.edges.size() >= 32768) throw std::runtime_error("Automatic graph edge budget exceeded");
        Object::Ptr value = new Object();
        value->set("from", from);
        value->set("to", to);
        value->set("relation", relation);
        value->set("source", origin);
        value->set("evidence", "typed-source-candidate");
        value->set("sourceCoverage", "representative-source");
        value->set("occurrences", 1u);
        edgeKeys.emplace(key, value);
        snapshot.edges.push_back(value);
    }
    std::string functionId(const std::string& name) const {
        auto found = functions.find(name);
        if (found == functions.end() || found->second.size() != 1) return "";
        return (found->second[0].file.starts_with("tests/") ? "test:" : "script:") + name;
    }
    void prepare() {
        for (const auto& api : registry.snapshot()) {
            if (api.semantics) factories[api.semantics->factory] = api.module;
            if (!api.scriptClass.empty()) {
                const auto key = api.scriptClass + '\0' + api.method;
                if (methods.contains(key))
                    methods[key] = nullptr;
                else
                    methods[key] = registry.find(api.key());
            }
        }
        for (const auto& [path, file] : snapshot.files) {
            if (!file->error.empty()) continue;
            for (const auto& value : file->structure.classes) {
                auto item = node("class:" + value.name, "script_class", source(path, value.line));
                if (item->has("fields") || item->getValue<std::string>("status") == "ambiguous") {
                    item->set("status", "ambiguous");
                    item->remove("fields");
                    for (auto& [id, field] : snapshot.nodes)
                        if (id.starts_with("field:" + value.name + ".")) {
                            field->set("status", "ambiguous");
                            field->remove("type");
                            field->remove("defaultValue");
                        }
                    continue;
                }
                Array::Ptr fields = new Array();
                for (const auto& binding : file->structure.bindings)
                    if (binding.scope == value.name + ".") {
                        Object::Ptr field = new Object();
                        field->set("name", binding.name);
                        field->set("type", binding.declaredType.empty() ? "dynamic" : binding.declaredType);
                        field->set("initializerKind", binding.expression.kind);
                        fields->add(field);
                        auto fieldId = "field:" + value.name + "." + binding.name;
                        auto indexed = node(fieldId, "script_field", source(path, binding.expression.line));
                        indexed->set("type", field->get("type"));
                        indexed->set("initializerKind", binding.expression.kind);
                        if (binding.expression.kind == "string" || binding.expression.kind == "number")
                            indexed->set("defaultValue", binding.expression.value);
                        edge("class:" + value.name, fieldId, "contains", source(path, binding.expression.line));
                    }
                item->set("fields", fields);
            }
            for (const auto& function : file->structure.functions) {
                functions[function.name].push_back({path, &function});
                for (const auto& parameter : function.parameters)
                    variables[localKey(path, function.name, parameter.name)].name = parameter.type;
            }
            for (const auto& binding : file->structure.bindings) {
                auto& value = variables[bindingKey(path, binding)];
                if (!binding.declaredType.empty()) value.name = binding.declaredType;
            }
        }
        for (int pass = 0; pass < 16; ++pass) {
            bool changed = false;
            for (const auto& [path, file] : snapshot.files) {
                if (!file->error.empty()) continue;
                for (const auto& binding : file->structure.bindings)
                    changed |=
                        merge(variables[bindingKey(path, binding)], type(binding.expression, path, binding.scope));
                for (const auto& call : file->structure.calls) {
                    const auto& expression = call.expression;
                    if (expression.operands.empty() || expression.operands[0].kind != "identifier") continue;
                    auto function = functions.find(expression.operands[0].value);
                    if (function == functions.end() || function->second.size() != 1) continue;
                    const auto& target = function->second[0];
                    for (size_t p = 0; p < target.syntax->parameters.size() && p + 1 < expression.operands.size(); ++p)
                        changed |= merge(
                            variables[localKey(target.file, target.syntax->name, target.syntax->parameters[p].name)],
                            type(expression.operands[p + 1], path, call.scope));
                }
            }
            if (!changed) break;
        }
        // A write outside the resolved expression subset invalidates every
        // possible static alias. Null initialization does not invalidate a ref.
        for (const auto& [path, file] : snapshot.files)
            for (const auto& binding : file->structure.bindings)
                if (type(binding.expression, path, binding.scope).name.empty())
                    variables[bindingKey(path, binding)].name = "dynamic";
        for (int pass = 0; pass < 16; ++pass)
            for (const auto& [path, file] : snapshot.files)
                for (const auto& binding : file->structure.bindings)
                    merge(variables[bindingKey(path, binding)], type(binding.expression, path, binding.scope));
        for (const auto& [name, locations] : functions) {
            if (locations.size() != 1) {
                Object::Ptr diagnostic = new Object();
                diagnostic->set("code", "ambiguous_function");
                diagnostic->set("id", name);
                snapshot.diagnostics.push_back(diagnostic);
                continue;
            }
            const auto& location   = locations[0];
            auto        item       = node(functionId(name), location.file.starts_with("tests/") ? "test" : "script",
                                          source(location.file, location.syntax->line));
            Array::Ptr  parameters = new Array();
            for (const auto& parameter : location.syntax->parameters) {
                Object::Ptr value = new Object();
                value->set("name", parameter.name);
                auto inferred = variable(location.file, name, parameter.name);
                value->set("type", inferred.name.empty() ? "dynamic" : inferred.name);
                parameters->add(value);
            }
            item->set("parameters", parameters);
            const auto dot = name.rfind('.');
            if (dot != std::string::npos)
                edge("class:" + name.substr(0, dot), functionId(name), "declares",
                     source(location.file, location.syntax->line));
        }
    }
    std::string jsonType(const Poco::Dynamic::Var& value) const {
        if (value.type() == typeid(Object::Ptr)) return "object";
        if (value.type() == typeid(Array::Ptr)) return "array";
        if (value.isString()) return "string";
        if (value.isBoolean()) return "boolean";
        if (value.isInteger()) return "integer";
        return value.isNumeric() ? "number" : "unknown";
    }
    void schemaObjects(const Poco::Dynamic::Var& value, const std::string& path, const std::string& pointer,
                       Object::Ptr schema, const std::string& apiKey) {
        if (value.type() == typeid(Object::Ptr)) {
            auto       object   = value.extract<Object::Ptr>();
            const auto identity = schema->getValue<std::string>("identityField");
            bool       matches  = object->has(identity) && object->get(identity).isString();
            for (const auto& field : *schema->getObject("fields"))
                matches &= object->has(field.first) &&
                           jsonType(object->get(field.first)) == field.second.convert<std::string>();
            if (matches) {
                auto id   = schema->getValue<std::string>("kind") + ":" + object->getValue<std::string>(identity);
                auto item = node(id, schema->getValue<std::string>("kind"), source(path, 0, pointer));
                if (item->has("definition")) {
                    item->set("status", "ambiguous");
                    item->remove("definition");
                    Object::Ptr diagnostic = new Object();
                    diagnostic->set("code", "ambiguous_schema_identity");
                    diagnostic->set("id", id);
                    snapshot.diagnostics.push_back(diagnostic);
                } else if (item->getValue<std::string>("status") != "ambiguous")
                    item->set("definition", object);
                item->set("schemaContract", apiKey);
                item->set("evidence", "engine-schema-shape");
            }
            for (const auto& field : *object) {
                std::string escaped;
                for (char c : field.first) escaped += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
                schemaObjects(field.second, path, pointer + "/" + escaped, schema, apiKey);
            }
        } else if (value.type() == typeid(Array::Ptr)) {
            auto array = value.extract<Array::Ptr>();
            for (size_t i = 0; i < array->size(); ++i)
                schemaObjects(array->get(static_cast<unsigned>(i)), path, pointer + "/" + std::to_string(i), schema,
                              apiKey);
        }
    }
    void project(std::string& version) {
        prepare();
        version += std::string(eve::script::scriptStructureProviderHash());
        for (const auto& api : registry.snapshot()) {
            if (!api.semantics) continue;
            version += api.key() + '\0' + api.toJson() + '\0';
            if (!api.semantics->schemaJson.empty()) {
                Poco::JSON::Parser parser;
                auto               schema = parser.parse(api.semantics->schemaJson).extract<Object::Ptr>();
                for (const auto& [path, file] : snapshot.files)
                    if (file->error.empty() && !file->json.isEmpty())
                        schemaObjects(file->json, path, "", schema, api.key());
            }
        }
        std::map<std::string, std::set<std::string>> nativeByFunction, calledFunctions;
        for (const auto& [path, file] : snapshot.files) {
            if (!file->error.empty()) continue;
            std::map<std::string, std::set<std::string>> filters, subjects;
            for (const auto& call : file->structure.calls) {
                const auto& expression = call.expression;
                if (expression.operands.empty()) continue;
                auto        from   = functionId(call.scope);
                auto        origin = source(path, expression.line);
                const auto& callee = expression.operands[0];
                if (callee.kind == "identifier") {
                    auto target = functionId(callee.value);
                    if (!from.empty() && !target.empty()) {
                        edge(from, target, "calls", origin);
                        calledFunctions[from].insert(target);
                    }
                }
                const auto* api = contract(expression, path, call.scope);
                if (!api && !from.empty() && callee.kind == "member" &&
                    !type(expression, path, call.scope).name.starts_with("module:")) {
                    auto item     = snapshot.nodes.at(from);
                    auto unknowns = item->getArray("unknownCalls");
                    if (!unknowns) {
                        unknowns = new Array();
                        item->set("unknownCalls", unknowns);
                    }
                    if (unknowns->size() < 16) {
                        Object::Ptr unknown = new Object();
                        unknown->set("method", callee.value);
                        unknown->set("source", origin);
                        unknown->set("reason", "receiver type unresolved or method ambiguous");
                        unknowns->add(unknown);
                    } else
                        item->set("unknownCallsTruncated", true);
                }
                if (!api) continue;
                auto               apiId = "api:" + api->key();
                auto               item  = node(apiId, "engine_api");
                Poco::JSON::Parser parser;
                item->set("contract", parser.parse(api->toJson()));
                item->set("evidence", "engine-build-contract");
                if (!from.empty()) {
                    edge(from, apiId, "uses", origin);
                    nativeByFunction[from].insert(apiId);
                }
                if (!api->semantics || api->semantics->role.empty()) continue;
                const auto& rule     = *api->semantics;
                auto        identity = argument(expression, rule.identityArgument, path, call.scope);
                if (rule.role == "scene_ref" && identity.values.size() == 1) {
                    auto host = argument(expression, rule.relatedArgument, path, call.scope);
                    if (host.values.size() == 1)
                        node("entity:" + *host.values.begin() + "/" + *identity.values.begin(), "scene_entity", origin);
                } else if (rule.role == "scene_write" && callee.kind == "member" && !callee.operands.empty()) {
                    auto target = type(callee.operands[0], path, call.scope);
                    if (target.identities.size() == 1 && target.hosts.size() == 1) {
                        auto entityId = "entity:" + *target.hosts.begin() + "/" + *target.identities.begin();
                        node(entityId, "scene_entity", origin);
                        edge(from, entityId, "updates", origin);
                        edge(entityId, apiId, "uses", origin);
                    } else
                        item->set("targetCoverage", "dynamic-or-ambiguous");
                } else if (rule.role == "event_emit" && identity.values.size() == 1) {
                    auto eventId = "event:" + *identity.values.begin();
                    node(eventId, "event", origin);
                    edge(from, eventId, "emits", origin);
                    auto subject = argument(expression, rule.relatedArgument, path, call.scope);
                    if (subject.values.size() == 1)
                        edge("quest:" + *subject.values.begin(), eventId, "subject_of", origin);
                } else if ((rule.role == "event_type" || rule.role == "event_subject") && !call.comparison.empty() &&
                           call.comparedValue.kind == "string") {
                    if (rule.role == "event_type") {
                        auto id = "event:" + call.comparedValue.value;
                        node(id, "event", origin);
                        edge(id, from, "filtered_by", origin);
                        filters[call.scope].insert(id);
                    } else
                        subjects[call.scope].insert(call.comparedValue.value);
                } else if (rule.role == "quest_read" || rule.role == "quest_complete") {
                    if (identity.values.size() == 1)
                        edge("quest:" + *identity.values.begin(), from,
                             rule.role == "quest_read" ? "read_by" : "completion_candidate", origin);
                }
            }
            for (const auto& [scope, events] : filters)
                for (const auto& event : events)
                    for (const auto& subject : subjects[scope])
                        edge("quest:" + subject, event, "subject_filter", source(path, 0));
        }
        // Test references come from source-directory ownership and actual direct
        // call reachability, not from a function name substring.
        for (const auto& [name, locations] : functions) {
            if (locations.size() != 1 || !locations[0].file.starts_with("tests/")) continue;
            std::set<std::string> reached{functionId(name)}, checked;
            for (int depth = 0; depth < 16; ++depth) {
                auto before  = reached.size();
                auto current = reached;
                for (const auto& function : current) {
                    checked.insert(nativeByFunction[function].begin(), nativeByFunction[function].end());
                    reached.insert(calledFunctions[function].begin(), calledFunctions[function].end());
                }
                if (reached.size() == before) break;
            }
            for (const auto& api : checked)
                edge(api, functionId(name), "tested_by", source(locations[0].file, locations[0].syntax->line));
        }
    }
};
}  // namespace
void automaticGraph(Snapshot& snapshot, std::string& version) {
    Analysis analysis{snapshot, eve::script::generatedBindingContracts(), {}, {}, {}, {}, {}};
    analysis.project(version);
}
}  // namespace eve::dev::project_index
