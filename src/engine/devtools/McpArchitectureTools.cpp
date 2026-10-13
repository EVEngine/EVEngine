#include "devtools/McpArchitectureTools.hpp"
#include <Poco/JSON/Parser.h>
#include <algorithm>
#include <map>
#include <queue>
#include <set>
#include "common/BindingContracts.h"
#include "common/ScriptCompiler.h"
#include "devtools/McpJson.hpp"
#include "devtools/McpProjectIndexTools.hpp"

namespace eve::dev {
namespace {
using Object = Poco::JSON::Object;
using Array  = Poco::JSON::Array;
struct Node {
    std::string id, kind, label, source, coverage;
};
struct Edge {
    std::string from, to, relation, evidence, fingerprint;
};
struct Graph {
    std::map<std::string, Node>                nodes;
    std::vector<Edge>                          edges;
    std::map<std::string, std::vector<size_t>> adjacency;
    void node(const std::string& id, const std::string& kind, const std::string& label, const std::string& source = {},
              const std::string& coverage = "known") {
        nodes.emplace(id, Node{id, kind, label, source, coverage});
    }
    void edge(const std::string& from, const std::string& to, const std::string& relation, const std::string& evidence,
              const std::string& fingerprint = {}) {
        const auto index = edges.size();
        edges.push_back({from, to, relation, evidence, fingerprint});
        adjacency[from].push_back(index);
        adjacency[to].push_back(index);
    }
};
Graph buildGraph() {
    Graph graph;
    for (const auto& contract : eve::script::generatedBindingContracts().snapshot()) {
        const auto api     = "api:" + contract.key();
        const auto binding = "binding:" + contract.key();
        graph.node(api, "api", contract.key(), {}, contract.architecture ? "mapped" : "unknown-owner");
        graph.node(binding, "binding", contract.key(), contract.source);
        graph.edge(api, binding, "binds", "static-binding");
        if (contract.cppSymbol.empty()) continue;
        const auto implementation = "implementation:" + contract.module + ":" + contract.cppSymbol;
        graph.node(implementation, "implementation", contract.cppSymbol,
                   contract.architecture ? contract.architecture->implementationSource : std::string{});
        graph.edge(binding, implementation, "calls", "static-direct-binding");
        if (!contract.architecture) continue;
        const auto& mapping = *contract.architecture;
        const auto  owner   = "owner:" + mapping.stateOwner;
        graph.node(owner, "owner", mapping.stateOwner, mapping.ownerSource);
        graph.edge(implementation, owner, "state_owner", "reviewed-owner", mapping.inputHash);
        const auto addAccess = [&](const std::vector<std::string>& fields, const std::string& access) {
            for (const auto& field : fields) {
                const auto state = "state:" + field;
                graph.node(state, "state", field);
                graph.edge(owner, state, "owns", "reviewed-owner", mapping.inputHash);
                graph.edge(implementation, state, access, "reviewed-access", mapping.inputHash);
            }
        };
        addAccess(mapping.writes, "writes");
        addAccess(mapping.reads, "reads");
        for (const auto& test : mapping.tests) {
            const auto id = "test:" + test;
            graph.node(id, "test", test, test);
            graph.edge(implementation, id, "tested_by", "test-reference", mapping.inputHash);
        }
    }
    return graph;
}
Object::Ptr nodeJson(const Node& node) {
    Object::Ptr out = new Object();
    out->set("id", node.id);
    out->set("kind", node.kind);
    out->set("label", node.label);
    out->set("source", node.source);
    out->set("coverage", node.coverage);
    return out;
}
Object::Ptr query(std::string_view mode, Object::Ptr args) {
    const auto seedArgument = args ? args->optValue<std::string>("seed", "") : std::string{};
    const auto seed         = seedArgument.find(':') == std::string::npos ? "api:" + seedArgument : seedArgument;
    const int  limit        = args ? args->optValue<int>("maxNodes", 64) : 64;
    const int  depth        = args ? args->optValue<int>("maxDepth", 6) : 6;
    const int  bytes        = args ? args->optValue<int>("maxBytes", 16384) : 16384;
    if (seedArgument.empty() || limit < 1 || limit > 256 || depth < 0 || depth > 8 || bytes < 1024 || bytes > 65536 ||
        (mode == "eve_task_context" && (!args || args->optValue<std::string>("task", "").empty())))
        return mcpFailureObject("invalid_query", "seed required; maxNodes 1..256, maxDepth 0..8, maxBytes 1024..65536");
    // Generated contracts are immutable for this linked process. Retain owning
    // evidence once; hot reload does not replace this build-generated registry.
    static const Graph graph = buildGraph();
    if (!graph.nodes.contains(seed))
        return mcpFailureObject("unknown_seed", "Use api: followed by a binding key or a returned node ID");
    const auto expectedHash = args->optValue<std::string>("expectedInputHash", "");
    if (!expectedHash.empty()) {
        const auto contract = eve::script::generatedBindingContracts().find(graph.nodes.at(seed).label);
        if (!contract || !contract->architecture || contract->architecture->inputHash != expectedHash)
            return mcpFailureObject("stale_evidence",
                                    "Refresh the seed contract fingerprint before using this task context");
    }
    std::set<std::string>                   selected{seed};
    std::queue<std::pair<std::string, int>> pending;
    pending.emplace(seed, 0);
    bool truncated = false;
    while (!pending.empty()) {
        const auto [current, level] = pending.front();
        pending.pop();
        for (const auto index : graph.adjacency.at(current)) {
            const auto& edge = graph.edges[index];
            const auto  next = edge.from == current ? edge.to : edge.from;
            if (selected.contains(next)) continue;
            // Impact flows from writers to field consumers; owner/test nodes
            // supply context but cannot fan out into unrelated state.
            bool terminal = false;
            if (mode != "eve_architecture_query") {
                const auto& kind = graph.nodes.at(current).kind;
                if (kind == "owner" || kind == "test") continue;
                if (edge.relation == "reads" && edge.from == current) {
                    if (mode == "eve_architecture_impact") continue;
                    terminal = true;
                }
            }
            if (level >= depth || selected.size() >= static_cast<size_t>(limit)) {
                truncated = true;
                continue;
            }
            selected.insert(next);
            if (!terminal) pending.emplace(next, level + 1);
        }
    }
    Object::Ptr result = new Object();
    result->set("ok", true);
    result->set("schema", "eve.architecture-graph");
    result->set("schemaVersion", 1);
    result->set("seed", seed);
    result->set("mode", std::string(mode));
    result->set("coverage", "reviewed-subset");
    result->set("limitations",
                "Candidate static impact; no runtime causality, dynamic keys or complete call graph. Test references "
                "do not prove tests passed.");
    Array::Ptr nodes = new Array(), edges = new Array(), tests = new Array(), unknowns = new Array(),
               fingerprints = new Array(), contracts = new Array();
    std::set<std::string> seenHashes;
    for (const auto& id : selected) {
        const auto& node = graph.nodes.at(id);
        nodes->add(nodeJson(node));
        if (node.kind == "api") {
            const auto* contract = eve::script::generatedBindingContracts().find(node.label);
            if (contract) {
                Poco::JSON::Parser parser;
                contracts->add(parser.parse(contract->toJson()));
            }
        }
        if (node.kind == "test") tests->add(node.label);
        if (node.coverage == "unknown-owner") unknowns->add(id);
    }
    for (const auto& edge : graph.edges) {
        if (!selected.contains(edge.from) || !selected.contains(edge.to)) continue;
        Object::Ptr item = new Object();
        item->set("from", edge.from);
        item->set("to", edge.to);
        item->set("relation", edge.relation);
        item->set("evidence", edge.evidence);
        item->set("inputHash", edge.fingerprint);
        edges->add(item);
        if (!edge.fingerprint.empty()) seenHashes.insert(edge.fingerprint);
    }
    for (const auto& hash : seenHashes) fingerprints->add(hash);
    result->set("nodes", nodes);
    result->set("edges", edges);
    result->set("tests", tests);
    result->set("unknowns", unknowns);
    result->set("inputHashes", fingerprints);
    result->set("contracts", contracts);
    result->set("truncated", truncated);
    if (mode == "eve_task_context") {
        result->set("schema", "eve.task-context");
        result->set("task", args->optValue<std::string>("task", ""));
        result->set("consistency", "build-generated source evidence; not a live-world snapshot");
    }
    // Return a bounded, explicit failure rather than silently discarding evidence.
    if (mcpStringify(result).size() > static_cast<size_t>(bytes))
        return mcpFailureObject("context_budget_exceeded",
                                "Reduce maxNodes/maxDepth or increase maxBytes; no partial evidence was returned");
    return result;
}
}  // namespace
bool isMcpArchitectureTool(std::string_view name) {
    return name == "eve_architecture_query" || name == "eve_architecture_impact" || name == "eve_task_context";
}
std::string callMcpArchitectureTool(std::string_view name, Object::Ptr args) {
    if (name == "eve_task_context" && args && args->has("projectDir")) return callMcpProjectIndexTool(name, args);
    Object::Ptr payload;
    try {
        payload = isMcpArchitectureTool(name) ? query(name, args) : mcpFailureObject("unknown_tool");
    } catch (const Poco::Exception&) {
        payload = mcpFailureObject("invalid_query", "Argument types must match the tool schema");
    }
    return textContentResult(mcpStringify(payload), !payload->getValue<bool>("ok"));
}
std::string_view mcpArchitectureToolSchemas() {
    static const std::string schemas = [] {
        Array::Ptr tools = new Array();
        for (const auto* name : {"eve_architecture_query", "eve_architecture_impact", "eve_task_context"}) {
            Poco::JSON::Parser parser;
            auto               input =
                parser
                    .parse(
                        R"JSON({"type":"object","required":["seed"],"properties":{"seed":{"type":"string"},"expectedInputHash":{"type":"string"},"maxNodes":{"type":"integer","minimum":1,"maximum":256},"maxDepth":{"type":"integer","minimum":0,"maximum":8},"maxBytes":{"type":"integer","minimum":1024,"maximum":65536}}})JSON")
                    .extract<Object::Ptr>();
            if (std::string_view(name) == "eve_task_context") {
                input->getArray("required")->add("task");
                Object::Ptr task = new Object();
                task->set("type", "string");
                input->getObject("properties")->set("task", task);
                for (const auto* field : {"projectDir", "expectedRevision"}) {
                    Object::Ptr value = new Object();
                    value->set("type", "string");
                    input->getObject("properties")->set(field, value);
                }
                Object::Ptr includeCalls = new Object();
                includeCalls->set("type", "boolean");
                input->getObject("properties")->set("includeCalls", includeCalls);
            }
            Object::Ptr tool = new Object();
            tool->set("name", std::string(name));
            tool->set("inputSchema", input);
            tool->set("description",
                      "Read-only bounded architecture evidence: query graph, candidate state-access impact, or task "
                      "context. Coverage is partial; no runtime causality. Stable API seed: api:module/Class.method.");
            tools->add(tool);
        }
        const auto text = mcpStringify(tools);
        return text.substr(1, text.size() - 2);
    }();
    return schemas;
}
}  // namespace eve::dev
