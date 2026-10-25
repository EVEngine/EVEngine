#include "devtools/McpProjectIndexTools.hpp"
#include <Poco/JSON/Array.h>
#include <algorithm>
#include <cstdint>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include "devtools/McpJson.hpp"
#include "devtools/McpToolFamilies.hpp"
#include "devtools/ProjectIndexInternal.hpp"

namespace eve::dev {
namespace {
using namespace project_index;
using Array = Poco::JSON::Array;
struct State {
    Snapshot snapshot;
    uint64_t used = 0;
};
std::map<std::string, State> states;
uint64_t                     sequence = 0;
bool                         sameFiles(const Files& left, const Files& right) {
    if (left.size() != right.size()) return false;
    for (const auto& [path, file] : left) {
        const auto other = right.find(path);
        if (other == right.end() || file->hash != other->second->hash) return false;
    }
    return true;
}
State& stateFor(const std::filesystem::path& root) {
    const auto key = pathText(root);
    if (!states.contains(key) && states.size() >= 4) {
        const auto oldest = std::min_element(states.begin(), states.end(), [](const auto& left, const auto& right) {
            return left.second.used < right.second.used;
        });
        states.erase(oldest);
    }
    auto& state = states[key];
    state.used  = ++sequence;
    return state;
}
void refresh(const std::filesystem::path& root, State& state) {
    Object::Ptr stats = new Object();
    auto        files = scan(root, state.snapshot.files, stats);
    if (!state.snapshot.revision.empty() && sameFiles(files, state.snapshot.files)) {
        state.snapshot.stats = stats;
        return;
    }
    auto candidate       = project(std::move(files), stats, pathText(root.filename()));
    candidate.projectDir = pathText(root);
    // Detect a changed read set before publication; this is not a filesystem
    // transaction. Consumers must recheck revision before a later action.
    Object::Ptr confirmation = new Object();
    if (!sameFiles(candidate.files, scan(root, candidate.files, confirmation)))
        throw std::runtime_error("Project changed during indexing; retry");
    state.snapshot = std::move(candidate);
}
Object::Ptr query(const Snapshot& snapshot, std::string_view mode, Object::Ptr args) {
    const int limit = args->optValue<int>("maxNodes", 64), depth = args->optValue<int>("maxDepth", 8);
    const int bytes = args->optValue<int>("maxBytes", 32768);
    if (limit < 1 || limit > 256 || depth < 0 || depth > 8 || bytes < 1024 || bytes > 65536)
        return mcpFailureObject("invalid_query", "maxNodes 1..256, maxDepth 0..8, maxBytes 1024..65536");
    const auto expected = args->optValue<std::string>("expectedRevision", "");
    if (!expected.empty() && expected != snapshot.revision) {
        auto failure = mcpFailureObject("stale_project_revision", "Refresh project evidence and use the new revision");
        failure->set("revision", snapshot.revision);
        return failure;
    }
    Object::Ptr out = new Object();
    out->set("ok", true);
    out->set("schema", mode == "eve_task_context" ? "eve.project-context" : "eve.project-index");
    out->set("schemaVersion", snapshot.automatic ? 2 : 1);
    out->set("projectId", snapshot.projectId);
    out->set("projectDir", snapshot.projectDir);
    out->set("revision", snapshot.revision);
    if (snapshot.automatic) out->set("analyzerHash", std::string(eve::script::scriptStructureProviderHash()));
    out->set("declarationsResolved", snapshot.diagnostics.empty());
    out->set("coverage", "partial-project");
    out->set("evidence",
             snapshot.automatic ? "compiler-structure+engine-contracts" : "source-metadata+checked-declarations");
    out->set("untrustedProjectContent", true);
    out->set("includeCalls", args->optValue<bool>("includeCalls", false));
    out->set("limitations",
             "Source evidence, not live state or runtime causality; symbol extraction is not compilation. Structural "
             "addresses may change; declared semantic IDs survive unique source relocation.");
    Array::Ptr diagnostics = new Array();
    for (const auto& item : snapshot.diagnostics) diagnostics->add(item);
    out->set("diagnostics", diagnostics);
    out->set("stats", snapshot.stats);
    Array::Ptr            nodes = new Array(), edges = new Array(), contracts = new Array(), tests = new Array();
    std::set<std::string> selected;
    bool                  truncated = false;
    if (mode == "eve_project_index_refresh") {
        for (const auto& [id, item] : snapshot.nodes)
            if (item->optValue<bool>("semantic", false) ||
                (item->has("evidence") && (item->getValue<std::string>("evidence") == "project-declaration" ||
                                           item->getValue<std::string>("evidence") == "engine-build-contract"))) {
                if (selected.size() < static_cast<size_t>(limit))
                    selected.insert(id);
                else
                    truncated = true;
            }
    } else {
        const auto seed = args->optValue<std::string>("seed", "");
        if (mode == "eve_task_context" && (seed.empty() || args->optValue<std::string>("task", "").empty()))
            return mcpFailureObject("invalid_query", "Project task context requires seed and task");
        if (!seed.empty()) {
            if (!snapshot.nodes.contains(seed)) return mcpFailureObject("unknown_seed");
            std::map<std::string, std::vector<size_t>> adjacency;
            for (size_t i = 0; i < snapshot.edges.size(); ++i) {
                const auto& edge = snapshot.edges[i];
                adjacency[edge->getValue<std::string>("from")].push_back(i);
                adjacency[edge->getValue<std::string>("to")].push_back(i);
            }
            std::queue<std::pair<std::string, int>> pending;
            selected.insert(seed);
            pending.emplace(seed, 0);
            while (!pending.empty()) {
                const auto [current, level] = pending.front();
                pending.pop();
                if (snapshot.automatic && !args->optValue<bool>("includeCalls", false) &&
                    snapshot.nodes.at(current)->getValue<std::string>("kind") == "test")
                    continue;
                for (const auto i : adjacency[current]) {
                    const auto& edge = snapshot.edges[i];
                    if (snapshot.automatic && !args->optValue<bool>("includeCalls", false) &&
                        snapshot.nodes.at(current)->getValue<std::string>("kind") == "engine_api" &&
                        edge->getValue<std::string>("relation") != "tested_by")
                        continue;
                    if (snapshot.automatic && !args->optValue<bool>("includeCalls", false) &&
                        edge->getValue<std::string>("relation") == "calls")
                        continue;
                    const auto from = edge->getValue<std::string>("from"), to = edge->getValue<std::string>("to");
                    const auto next = from == current ? to : from;
                    if (selected.contains(next)) continue;
                    if (level >= depth || selected.size() >= static_cast<size_t>(limit)) {
                        truncated = true;
                        continue;
                    }
                    selected.insert(next);
                    pending.emplace(next, level + 1);
                }
            }
        } else {
            const auto kind = args->optValue<std::string>("kind", ""), text = args->optValue<std::string>("query", "");
            for (const auto& [id, item] : snapshot.nodes) {
                if (!kind.empty() && item->getValue<std::string>("kind") != kind) continue;
                if (!text.empty() && id.find(text) == std::string::npos &&
                    item->getValue<std::string>("label").find(text) == std::string::npos)
                    continue;
                if (selected.size() >= static_cast<size_t>(limit)) {
                    truncated = true;
                    continue;
                }
                selected.insert(id);
            }
        }
    }
    for (const auto& id : selected) {
        const auto& item = snapshot.nodes.at(id);
        if (snapshot.automatic && mode == "eve_project_index_refresh") {
            Object::Ptr summary = new Object();
            for (const auto* field : {"id", "kind", "label", "status", "source", "evidence", "schemaContract"})
                if (item->has(field)) summary->set(field, item->get(field));
            nodes->add(summary);
            if (item->getValue<std::string>("kind") == "test") tests->add(summary);
            continue;
        }
        if (snapshot.automatic && item->has("contract")) {
            Object::Ptr indexed = new Object();
            for (const auto& field : *item)
                if (field.first != "contract") indexed->set(field.first, field.second);
            indexed->set("contractKey", id.substr(4));
            nodes->add(indexed);
        } else
            nodes->add(item);
        if (item->has("contract")) contracts->add(item->get("contract"));
        if (item->getValue<std::string>("kind") == "test") tests->add(item);
    }
    for (const auto& edge : snapshot.edges)
        if (!(snapshot.automatic &&
              (mode == "eve_project_index_refresh" ||
               (!args->optValue<bool>("includeCalls", false) && edge->getValue<std::string>("relation") == "calls"))) &&
            selected.contains(edge->getValue<std::string>("from")) &&
            selected.contains(edge->getValue<std::string>("to")))
            edges->add(edge);
    out->set("nodes", nodes);
    out->set("edges", edges);
    out->set("contracts", contracts);
    out->set("tests", tests);
    out->set("truncated", truncated);
    if (mode == "eve_task_context") out->set("task", args->getValue<std::string>("task"));
    if (mcpStringify(out).size() > static_cast<size_t>(bytes))
        return mcpFailureObject("context_budget_exceeded",
                                "Reduce maxNodes/depth or increase maxBytes; no partial evidence returned");
    return out;
}
}  // namespace
bool isMcpProjectIndexTool(std::string_view name) {
    return name == "eve_project_index_refresh" || name == "eve_project_index_query";
}
std::string callMcpProjectIndexTool(std::string_view name, Object::Ptr args) {
    Object::Ptr result;
    std::string lastRevision;
    try {
        if (!isMcpProjectIndexTool(name) && name != "eve_task_context")
            return textContentResult(mcpStringify(mcpFailureObject("unknown_tool")), true);
        const auto root = std::filesystem::canonical(sourcePath(requiredString(args, "projectDir")));
        if (!std::filesystem::is_directory(root) || std::filesystem::is_symlink(root / "game.semantic.json") ||
            (std::filesystem::exists(root / "game.semantic.json") &&
             !std::filesystem::is_regular_file(root / "game.semantic.json")))
            throw std::runtime_error("Project requires a directory and an optional regular game.semantic.json");
        auto& state  = stateFor(root);
        lastRevision = state.snapshot.revision;
        refresh(root, state);
        result = query(state.snapshot, name, args);
    } catch (const std::exception& error) {
        result = mcpFailureObject("project_index_failed", error.what());
        if (!lastRevision.empty()) result->set("lastGoodRevision", lastRevision);
    }
    return textContentResult(mcpStringify(result), !result->getValue<bool>("ok"));
}
std::string_view mcpProjectIndexToolSchemas() {
    return R"JSON({"name":"eve_project_index_refresh","description":"Index current project source and checked semantic declarations without executing code. Reports revision, incremental parse stats and diagnostics.","inputSchema":{"type":"object","required":["projectDir"],"properties":{"projectDir":{"type":"string"},"maxNodes":{"type":"integer","minimum":1,"maximum":256},"maxBytes":{"type":"integer","minimum":1024,"maximum":65536}}}},
{"name":"eve_project_index_query","description":"Query compiler-derived project semantics by seed, kind or text. includeCalls expands the full static call neighbourhood. Rejects stale expectedRevision; does not execute project code.","inputSchema":{"type":"object","required":["projectDir"],"properties":{"projectDir":{"type":"string"},"seed":{"type":"string"},"kind":{"type":"string"},"query":{"type":"string"},"includeCalls":{"type":"boolean"},"expectedRevision":{"type":"string"},"maxNodes":{"type":"integer","minimum":1,"maximum":256},"maxDepth":{"type":"integer","minimum":0,"maximum":8},"maxBytes":{"type":"integer","minimum":1024,"maximum":65536}}}})JSON";
}
}  // namespace eve::dev

namespace eve::dev {
EVE_MCP_TOOL_FAMILY(ProjectIndex, mcpProjectIndexToolSchemas, callMcpProjectIndexTool, ContentEnvelope)
}  // namespace eve::dev
