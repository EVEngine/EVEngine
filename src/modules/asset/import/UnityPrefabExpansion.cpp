#include <regex>
#include <set>
#include <stdexcept>
#include "asset/import/UnityImporter.h"
#include "asset/import/UnitySourceInternal.h"

namespace eve::asset_import {
namespace {
struct Doc {
    std::string type, id, body;
    bool        stripped = false;
};
std::string find(const std::string& text, const std::string& pattern) {
    std::smatch m;
    return std::regex_search(text, m, std::regex(pattern)) ? m[1].str() : std::string{};
}
std::vector<Doc> docs(const std::string& text) {
    std::vector<Doc> out;
    std::size_t      start = 0;
    const std::regex header(R"((?:^|\n)--- !u!([0-9]+) &(-?[0-9]+)([^\n]*)\n)");
    for (std::sregex_iterator it(text.begin(), text.end(), header), end; it != end; ++it) {
        if (!out.empty()) out.back().body = text.substr(start, std::size_t(it->position()) - start);
        out.push_back({(*it)[1], (*it)[2], {}, (*it)[3].str().find("stripped") != std::string::npos});
        start = std::size_t(it->position() + it->length());
    }
    if (!out.empty()) out.back().body = text.substr(start);
    return out;
}
void replaceCapture(std::string& body, const std::string& pattern, const std::string& value) {
    std::smatch m;
    if (!std::regex_search(body, m, std::regex(pattern))) throw std::runtime_error("Prefab override field missing");
    body.replace(std::size_t(m.position(1)), std::size_t(m.length(1)), value);
}
struct Expander {
    const UnityProjectImportRequest&   request;
    std::map<std::string, std::string> paths;
    std::set<std::string>              visiting;
    std::vector<ImportFinding>         findings;
    std::vector<Doc>                   expand(const std::string& guid, unsigned depth) {
        if (depth > 32 || !visiting.insert(guid).second)
            throw std::runtime_error("Prefab dependency cycle or depth budget exceeded");
        if (!paths.contains(guid)) throw std::runtime_error("Prefab source GUID is missing");
        const auto& path  = paths.at(guid);
        const auto& bytes = request.files.at(path);
        std::string text(bytes.begin(), bytes.end());
        std::erase(text, '\r');
        auto                  original = docs(text);
        std::vector<Doc>      out;
        std::set<std::string> used;
        for (const auto& d : original)
            if (!used.insert(d.id).second) throw std::runtime_error("duplicate Prefab fileID");
        for (const auto& d : original)
            if (d.type != "1001" && !d.stripped) out.push_back(d);
        for (const auto& instance : original) {
            if (instance.type != "1001") continue;
            const auto version = find(instance.body, R"(serializedVersion: *([0-9]+))");
            if (!version.empty() && version != "2") throw std::runtime_error("unsupported PrefabInstance version");
            if (instance.body.find("m_RemovedComponents:") == std::string::npos)
                throw std::runtime_error("Prefab removed-component list missing");
            const auto source = find(instance.body, R"(m_SourcePrefab: *\{fileID: *[0-9]+, guid: *([0-9a-fA-F]{32}))");
            const auto parent = find(instance.body, R"(m_TransformParent: *\{fileID: *(-?[0-9]+)\})");
            if (source.empty() || parent.empty()) throw std::runtime_error("Prefab instance source or parent missing");
            auto             children = expand(unity_detail::foldAscii(source), depth + 1);
            const auto       removed = find(instance.body, R"(m_RemovedComponents:([\s\S]*?)\n  m_SourcePrefab:)");
            const std::regex removal(R"(fileID: (-?[0-9]+), guid: ([0-9a-fA-F]{32}))");
            for (std::sregex_iterator it(removed.begin(), removed.end(), removal), end; it != end; ++it) {
                if (unity_detail::foldAscii((*it)[2]) != unity_detail::foldAscii(source))
                    throw std::runtime_error("removed-component GUID mismatch");
                const auto id = (*it)[1].str();
                for (const auto& child : children)
                    if (child.id == id && (child.type == "1" || child.type == "4"))
                        throw std::runtime_error("cannot remove hierarchy as component");
                std::erase_if(children, [&](const auto& child) { return child.id == id; });
            }
            std::map<std::string, std::string> ids;
            for (const auto& stripped : original)
                if (stripped.stripped &&
                    find(stripped.body, R"(m_PrefabInstance: *\{fileID: *(-?[0-9]+)\})") == instance.id) {
                    const auto target = find(stripped.body, R"(m_CorrespondingSourceObject: *\{fileID: *(-?[0-9]+),)");
                    if (target.empty() || !ids.emplace(target, stripped.id).second)
                        throw std::runtime_error("invalid stripped Prefab alias");
                }
            std::map<std::string, std::size_t> byId;
            for (std::size_t i = 0; i < children.size(); ++i) byId.emplace(children[i].id, i);
            const std::regex modification(
                R"(    - target: \{fileID: (-?[0-9]+), guid: ([0-9a-fA-F]{32}),\s*type: [0-9]+\}\n      propertyPath: ([^\n]+)\n      value:([^\n]*)\n      objectReference: (\{[^\n]+\}))");
            for (std::sregex_iterator it(instance.body.begin(), instance.body.end(), modification), end; it != end;
                 ++it) {
                if (unity_detail::foldAscii((*it)[2]) != unity_detail::foldAscii(source))
                    throw std::runtime_error("Prefab override source GUID mismatch");
                const auto id = (*it)[1].str(), property = (*it)[3].str();
                auto       value = (*it)[4].str();
                if (!value.empty() && value.front() == ' ') value.erase(0, 1);
                if (!byId.contains(id)) {
                    findings.push_back({path, "Prefab.unusedOverride", ImportDisposition::PreservedSource,
                                                          "override target no longer exists in source prefab: " + id + " " + property});
                    continue;
                }
                auto& target = children[byId.at(id)];
                if (target.type != "1" && target.type != "4" && target.type != "23" && target.type != "33") {
                    findings.push_back({path, "Prefab.nonVisualOverride", ImportDisposition::Unsupported,
                                                          "override on a non-converted component: " + property});
                    continue;
                }
                if (property == "m_RootOrder" || property == "m_StaticEditorFlags" ||
                    property.starts_with("m_LocalEulerAnglesHint."))
                    continue;
                if (property.starts_with("m_LocalPosition.") || property.starts_with("m_LocalRotation.") ||
                    property.starts_with("m_LocalScale.")) {
                    const auto dot   = property.find('.');
                    const auto field = property.substr(0, dot);
                    const auto axis  = property.substr(dot + 1);
                    if (axis.size() != 1 || std::string("xyzw").find(axis) == std::string::npos)
                        throw std::runtime_error("invalid transform override axis");
                    replaceCapture(target.body, field + ": *\\{[^}]*?" + axis + ": *([^,}]+)", value);
                } else if (property == "m_Name" || property == "m_IsActive" || property == "m_Enabled") {
                    replaceCapture(target.body, "(?:^|\\n)  " + property + ": *([^\\n]*)", value);
                } else if (property == "m_Mesh") {
                    replaceCapture(target.body, R"(m_Mesh: *(\{[^\n]+\}))", (*it)[5]);
                } else
                    throw std::runtime_error("unsupported visual Prefab override: " + property);
            }
            for (const auto& child : children)
                if (!ids.contains(child.id)) {
                    auto identity =
                        request.package.packageId.child("prefab-expand:" + guid + ":" + instance.id + ":" + child.id);
                    std::uint64_t value = 0;
                    for (unsigned i = 0; i < 8; ++i) value = (value << 8) | identity.bytes()[i + 8];
                    value &= 0x7fffffffffffffffull;
                    const auto id = std::to_string(value);
                    if (value == 0 || !used.insert(id).second)
                        throw std::runtime_error("expanded Prefab identity collision");
                    ids.emplace(child.id, id);
                }
            for (auto& child : children) {
                if (child.type == "4" && find(child.body, R"(m_Father: *\{fileID: *(-?[0-9]+)\})") == "0")
                    replaceCapture(child.body, R"(m_Father: *(\{fileID: *0\}))", "{fileID: __parent__}");
                const std::regex local(R"(\{fileID: (-?[0-9]+)\})");
                std::string      rewritten;
                std::size_t      start = 0;
                for (std::sregex_iterator it(child.body.begin(), child.body.end(), local), end; it != end; ++it) {
                    rewritten += child.body.substr(start, std::size_t(it->position()) - start);
                    const auto id = (*it)[1].str();
                    rewritten += "{fileID: " + (ids.contains(id) ? ids.at(id) : id) + "}";
                    start = std::size_t(it->position() + it->length());
                }
                rewritten += child.body.substr(start);
                const auto marker = rewritten.find("__parent__");
                if (marker != std::string::npos) rewritten.replace(marker, 10, parent);
                child.body     = std::move(rewritten);
                child.id       = ids.at(child.id);
                child.stripped = false;
                out.push_back(std::move(child));
                if (out.size() > request.limits.maximumAssets)
                    throw std::runtime_error("expanded Prefab object budget exceeded");
            }
        }
        visiting.erase(guid);
        return out;
    }
};
}  // namespace
Result<UnityExpandedPrefab> expandUnityPrefab(const UnityProjectImportRequest& request, const UnitySourceIndex& index,
                                              const UnitySourceAsset& source) {
    try {
        Expander expander{request, {}, {}, {}};
        for (const auto& asset : index.assets)
            if (asset.kind == UnitySourceKind::Prefab) expander.paths.emplace(asset.guid, asset.path);
        auto        expanded = expander.expand(source.guid, 0);
        std::string text     = "%YAML 1.1\n";
        for (const auto& doc : expanded) {
            text += "--- !u!" + doc.type + " &" + doc.id + "\n" + doc.body;
            if (text.back() != '\n') text += '\n';
            if (text.size() > request.limits.maximumSourceBytes)
                throw std::runtime_error("expanded Prefab byte budget exceeded");
        }
        return Result<UnityExpandedPrefab>::success({{text.begin(), text.end()}, std::move(expander.findings)});
    } catch (const std::exception& e) {
        return unity_detail::failure<UnityExpandedPrefab>(DiagnosticCode::ParseError, e.what(), source.path);
    }
}
}  // namespace eve::asset_import
