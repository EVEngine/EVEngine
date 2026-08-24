#include "ui/EditorHost.h"

#include "common/config.h"

#if defined(EVENGINE_WEBGPU) && defined(__EMSCRIPTEN__)

namespace eve::ui {

std::string EditorHost::reloadResource(const std::string&) {
    return "error: editor host hot reload unavailable on this platform";
}

std::string EditorHost::hotReloadStatus() const {
    return "{\"enabled\":false,\"watchCount\":0,\"reloadCount\":0,\"failureCount\":0}";
}

void EditorHost::setHotReloadWatchCount(int) {}

}  // namespace eve::ui

#else

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>

namespace eve::ui {
namespace {

struct HotReloadState {
    int         watchCount   = 0;
    int         reloadCount  = 0;
    int         failureCount = 0;
    std::string lastPath;
    std::string lastResult = "idle";
};

std::mutex     g_hotReloadMutex;
HotReloadState g_hotReloadState;

std::string stringify(const Poco::Dynamic::Var& value) {
    std::ostringstream out;
    Poco::JSON::Stringifier::stringify(value, out, 0, 0);
    return out.str();
}

bool endsWith(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string normalizedRelativePath(const std::string& root, const std::string& input, std::filesystem::path* absolute) {
    if (input.empty()) return {};
    std::error_code             ec;
    const std::filesystem::path canonicalRoot = std::filesystem::weakly_canonical(root, ec);
    if (ec) return {};
    std::filesystem::path candidate(input);
    if (!candidate.is_absolute()) candidate = canonicalRoot / candidate;
    candidate = std::filesystem::weakly_canonical(candidate, ec);
    if (ec) return {};

    std::filesystem::path relative = std::filesystem::relative(candidate, canonicalRoot, ec);
    if (ec || relative.empty()) return {};
    for (const auto& part : relative) {
        if (part == "..") return {};
    }
    if (absolute) *absolute = candidate;
    std::string path = relative.generic_string();
    while (path.rfind("./", 0) == 0) path.erase(0, 2);
    return path;
}

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::map<std::string, Poco::Dynamic::Var> editorValues(EditorHost& host, const std::string& editorId) {
    std::map<std::string, Poco::Dynamic::Var> values;
    try {
        Poco::JSON::Parser      parser;
        Poco::JSON::Object::Ptr root    = parser.parse(host.editorState(editorId)).extract<Poco::JSON::Object::Ptr>();
        Poco::JSON::Array::Ptr  editors = root ? root->getArray("editors") : nullptr;
        Poco::JSON::Object::Ptr editor  = editors && editors->size() ? editors->getObject(0) : nullptr;
        Poco::JSON::Object::Ptr object  = editor ? editor->getObject("values") : nullptr;
        if (object) {
            for (const auto& key : object->getNames()) values[key] = object->get(key);
        }
    } catch (...) {
    }
    return values;
}

void restoreEditorValues(EditorHost& host, const std::string& editorId,
                         const std::map<std::string, Poco::Dynamic::Var>& values) {
    for (const auto& [widget, value] : values) host.restoreEditorValue(editorId, widget, stringify(value));
}

std::string editorVmName(EditorHost& host, const std::string& editorId) {
    try {
        Poco::JSON::Parser      parser;
        Poco::JSON::Object::Ptr root    = parser.parse(host.listEditors()).extract<Poco::JSON::Object::Ptr>();
        Poco::JSON::Array::Ptr  editors = root ? root->getArray("editors") : nullptr;
        if (!editors) return {};
        for (size_t i = 0; i < editors->size(); ++i) {
            Poco::JSON::Object::Ptr editor = editors->getObject(static_cast<unsigned>(i));
            if (editor && editor->optValue<std::string>("id", "") == editorId)
                return editor->optValue<std::string>("vm", "");
        }
    } catch (...) {
    }
    return {};
}

std::string editorIdFromPath(const std::string& path, const std::string& suffix) {
    const std::string prefix = "editors/";
    if (path.rfind(prefix, 0) != 0 || !endsWith(path, suffix)) return {};
    const std::string id = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
    return id.find('/') == std::string::npos ? id : std::string();
}

std::string editorIdFromSource(const std::string& source) {
    try {
        Poco::JSON::Parser            parser;
        const Poco::JSON::Object::Ptr root = parser.parse(source).extract<Poco::JSON::Object::Ptr>();
        return root ? root->optValue<std::string>("id", "") : std::string();
    } catch (...) {
        return {};
    }
}

void recordReload(const std::string& path, const std::string& result) {
    std::lock_guard<std::mutex> lock(g_hotReloadMutex);
    g_hotReloadState.lastPath   = path;
    g_hotReloadState.lastResult = result;
    if (result.rfind("error:", 0) == 0)
        ++g_hotReloadState.failureCount;
    else
        ++g_hotReloadState.reloadCount;
}

}  // namespace

std::string EditorHost::reloadResource(const std::string& input) {
    if (!running()) return "error: host not started";
    std::filesystem::path absolute;
    const std::string     path = normalizedRelativePath(gameRoot(), input, &absolute);
    if (path.empty()) return "error: resource path is outside project root";
    if (!std::filesystem::is_regular_file(absolute)) {
        const std::string result = "error: resource not found: " + path;
        recordReload(path, result);
        return result;
    }

    const std::string source = readText(absolute);
    if (source.empty()) {
        const std::string result = "error: resource is empty: " + path;
        recordReload(path, result);
        return result;
    }

    std::string       result;
    const std::string editorSuffix = ".editor.json";
    const std::string vmSuffix     = ".vm.nut";
    const std::string editorId     = editorIdFromPath(path, editorSuffix);
    const std::string vmEditorId   = editorIdFromPath(path, vmSuffix);
    if (!editorId.empty()) {
        const std::string sourceId = editorIdFromSource(source);
        if (sourceId.empty()) {
            result = "error: editor resource requires a valid id";
        } else if (sourceId != editorId) {
            result = "error: editor id must match resource filename: " + editorId;
        } else {
            const auto values = editorValues(*this, editorId);
            result            = applyEditor(source);
            if (result.rfind("error:", 0) != 0) restoreEditorValues(*this, editorId, values);
        }
    } else if (!vmEditorId.empty()) {
        const std::string vmName = editorVmName(*this, vmEditorId);
        if (vmName.empty()) {
            result = "error: editor ViewModel not found for: " + vmEditorId;
        } else {
            const auto values = editorValues(*this, vmEditorId);
            result            = registerVM(vmName, source);
            if (result.rfind("error:", 0) != 0) restoreEditorValues(*this, vmEditorId, values);
        }
    } else if (path == "mcp.nut" || (path.rfind("mcp/", 0) == 0 && endsWith(path, ".nut"))) {
        result = runScript(source);
    } else {
        result = "error: unsupported MCP host resource: " + path;
    }
    recordReload(path, result);
    return result;
}

std::string EditorHost::hotReloadStatus() const {
    std::lock_guard<std::mutex> lock(g_hotReloadMutex);
    Poco::JSON::Object::Ptr     out(new Poco::JSON::Object());
    out->set("enabled", g_hotReloadState.watchCount > 0);
    out->set("watchCount", g_hotReloadState.watchCount);
    out->set("reloadCount", g_hotReloadState.reloadCount);
    out->set("failureCount", g_hotReloadState.failureCount);
    out->set("lastPath", g_hotReloadState.lastPath);
    out->set("lastResult", g_hotReloadState.lastResult);
    return stringify(Poco::Dynamic::Var(out));
}

void EditorHost::setHotReloadWatchCount(int count) {
    std::lock_guard<std::mutex> lock(g_hotReloadMutex);
    g_hotReloadState.watchCount = std::max(0, count);
}

}  // namespace eve::ui

#endif
