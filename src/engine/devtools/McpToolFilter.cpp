#include "devtools/McpToolFilter.hpp"

#include "devtools/McpJson.hpp"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace eve::dev {
namespace {

std::string toLower(std::string_view value) {
    std::string out(value);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::vector<std::string> splitComma(std::string_view raw) {
    std::vector<std::string> out;
    std::size_t              start = 0;
    while (start < raw.size()) {
        const std::size_t comma = raw.find(',', start);
        const auto        end   = comma == std::string_view::npos ? raw.size() : comma;
        std::string       part  = toLower(raw.substr(start, end - start));
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) part.erase(part.begin());
        while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) part.pop_back();
        if (!part.empty()) out.push_back(std::move(part));
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return out;
}

struct FilterConfig {
    bool                            all      = true;
    bool                            readOnly = false;
    std::unordered_set<std::string> tags;
    std::unordered_set<std::string> exclude;
};

std::mutex    g_mu;
FilterConfig  g_cfg;
bool          g_loaded = false;

void loadConfigUnlocked() {
    g_cfg = FilterConfig{};
    if (const char* toolset = std::getenv("EVE_MCP_TOOLSET")) {
        const auto parts = splitComma(toolset);
        g_cfg.all          = parts.empty();
        for (const auto& part : parts) {
            if (part == "all") {
                g_cfg.all = true;
                g_cfg.tags.clear();
                break;
            }
            g_cfg.all = false;
            g_cfg.tags.insert(part);
        }
    }
    if (const char* ro = std::getenv("EVE_MCP_READONLY")) {
        const std::string value = toLower(ro);
        g_cfg.readOnly =
            !(value.empty() || value == "0" || value == "false" || value == "off" || value == "no");
    }
    if (const char* exclude = std::getenv("EVE_MCP_EXCLUDE")) {
        for (auto& name : splitComma(exclude)) g_cfg.exclude.insert(std::move(name));
    }
    g_loaded = true;
}

const FilterConfig& config() {
    std::lock_guard lock(g_mu);
    if (!g_loaded) loadConfigUnlocked();
    return g_cfg;
}

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool equalsAny(std::string_view value, std::initializer_list<const char*> names) {
    for (const char* name : names) {
        if (value == name) return true;
    }
    return false;
}

}  // namespace

void mcpToolFilterReloadForTest() {
    std::lock_guard lock(g_mu);
    loadConfigUnlocked();
}

std::vector<std::string> mcpToolTags(std::string_view name) {
    std::vector<std::string> tags;
    auto                     add = [&](const char* tag) { tags.emplace_back(tag); };
    if (startsWith(name, "eve_frame_")) {
        add("core");
        add("scene");
        return tags;
    }

    if (startsWith(name, "eve_host_")) {
        add("host");
        return tags;
    }
    if (startsWith(name, "eve_editor_") || name == "eve_renderable3d_get") {
        add("editor");
        return tags;
    }
    if (startsWith(name, "eve_input_") || name == "eve_whitebox_playtest") {
        add("input");
        add("play");
        add("scene");
        return tags;
    }
    if (startsWith(name, "eve_agent_")) {
        add("agent");
        return tags;
    }
    if (name == "eve_game_world") {
        add("core");
        add("play");
        return tags;
    }
    if (startsWith(name, "eve_pixelworld_") || name == "eve_play" || name == "eve_gameplay" ||
        startsWith(name, "eve_ui_")) {
        add("play");
        return tags;
    }
    if (startsWith(name, "eve_scene_") || startsWith(name, "inspect_") || name == "set_camera_pose" ||
        name == "capture_render_frame" || name == "get_visible_entities_screen_bbox" ||
        startsWith(name, "eve_camera_")) {
        add("scene");
        return tags;
    }
    if (equalsAny(name, {"eve_eval", "eve_run_script", "eve_pause", "eve_continue", "eve_step_in", "eve_step_over",
                          "eve_step_out", "eve_stack", "eve_locals", "eve_set_breakpoint", "eve_clear_breakpoint",
                          "eve_list_breakpoints", "eve_watch_add", "eve_watch_list", "eve_snapshot_capture",
                          "eve_snapshot_restore", "eve_snapshot_save", "eve_snapshot_load", "eve_error_slice",
                          "eve_skeleton_inspect"})) {
        add("debug");
        return tags;
    }
    if (startsWith(name, "eve_decal_") || startsWith(name, "eve_profiler_") || name == "eve_sensing_last_query" ||
        name == "eve_world_project_down" || name == "eve_physics_sphere_cast" || startsWith(name, "eve_physics_") ||
        startsWith(name, "eve_procgen_") || startsWith(name, "eve_particles_") || startsWith(name, "eve_audio_")) {
        add("domain");
        return tags;
    }
    add("core");
    return tags;
}

bool mcpToolMutates(std::string_view name) {
    if (name == "eve_frame_capture_export") return true;
    if (startsWith(name, "eve_input_")) return name != "eve_input_poll" && name != "eve_input_status";
    if (name == "eve_eval" || name == "eve_run_script" || name == "eve_restart") return true;
    if (name == "eve_pause" || name == "eve_continue" || startsWith(name, "eve_step_")) return true;
    if (startsWith(name, "eve_set_breakpoint") || startsWith(name, "eve_clear_breakpoint") ||
        startsWith(name, "eve_watch_") || startsWith(name, "eve_snapshot_"))
        return true;
    if (name == "eve_ui_click" || name == "eve_play" || name == "eve_gameplay") return true;
    if (name == "eve_scene_modify" || name == "eve_scene_batch" || name == "eve_scene_reset" ||
        name == "eve_scene_node_set" || name == "set_camera_pose" || name == "eve_camera_generate" ||
        name == "eve_whitebox_playtest")
        return true;
    if (startsWith(name, "eve_editor_") &&
        (name.find("execute") != std::string_view::npos || name.find("commit") != std::string_view::npos ||
         name.find("plan") != std::string_view::npos || name.find("create") != std::string_view::npos ||
         name.find("close") != std::string_view::npos || name.find("cancel") != std::string_view::npos ||
         name.find("undo") != std::string_view::npos || name.find("redo") != std::string_view::npos ||
         name.find("observe_start") != std::string_view::npos || name.find("observe_close") != std::string_view::npos))
        return true;
    if (startsWith(name, "eve_host_") && name != "eve_host_status" && name != "eve_host_window_state" &&
        name != "eve_host_editor_list" && name != "eve_host_editor_state" && name != "eve_host_events" &&
        name != "eve_host_widget_rect" && name != "eve_host_hot_reload_status" && name != "eve_host_capture")
        return true;
    if (startsWith(name, "eve_pixelworld_") && name.find("validate") == std::string_view::npos &&
        name.find("diagnostics") == std::string_view::npos && name.find("samples") == std::string_view::npos &&
        name.find("list") == std::string_view::npos && name != "eve_pixelworld_worlds")
        return true;
    if (startsWith(name, "eve_decal_") && name != "eve_decal_status") return true;
    if (startsWith(name, "eve_physics_") && name != "eve_physics_list_worlds" && name != "eve_physics_raycast" &&
        name != "eve_physics_sphere_cast")
        return true;
    if (startsWith(name, "eve_particles_") && name != "eve_particles_status") return true;
    if (startsWith(name, "eve_audio_") && name != "eve_audio_status") return true;
    if (startsWith(name, "eve_procgen_") && name != "eve_procgen_recipes") return true;
    if (startsWith(name, "eve_agent_") && name != "eve_agent_session_status" && name != "eve_agent_session_evidence")
        return true;
    if (name == "eve_console_write" || name == "eve_console_clear" || name == "eve_ai_note") return true;
    if (name == "eve_render_vision_config") return true;
    return false;
}

bool mcpToolAllowed(std::string_view name) {
    const FilterConfig& cfg = config();
    if (cfg.exclude.count(toLower(name)) != 0) return false;
    if (cfg.readOnly && mcpToolMutates(name)) return false;
    if (cfg.all) return true;
    const auto tags = mcpToolTags(name);
    for (const auto& tag : tags) {
        if (cfg.tags.count(tag) != 0) return true;
    }
    return false;
}

std::string filterMcpToolsBody(std::string_view toolsBody) {
    if (toolsBody.empty()) return {};
    const std::string wrapped = std::string("[") + std::string(toolsBody) + "]";
    try {
        Poco::JSON::Parser parser;
        auto               array = parser.parse(wrapped).extract<Poco::JSON::Array::Ptr>();
        if (!array) return std::string(toolsBody);
        Poco::JSON::Array::Ptr kept = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        for (std::size_t i = 0; i < array->size(); ++i) {
            auto tool = array->getObject(static_cast<unsigned>(i));
            if (!tool || !tool->has("name")) continue;
            const std::string name = tool->getValue<std::string>("name");
            if (mcpToolAllowed(name)) kept->add(tool);
        }
        if (kept->size() == 0) return {};
        std::string out = mcpStringify(Poco::Dynamic::Var(kept));
        if (out.size() >= 2 && out.front() == '[' && out.back() == ']') return out.substr(1, out.size() - 2);
        return out;
    } catch (...) {
        return std::string(toolsBody);
    }
}

std::string mcpToolFilteredPayload(std::string_view name) {
    Poco::JSON::Object::Ptr out = mcpFailureObject(
        "tool '" + std::string(name) + "' is hidden by EVE_MCP_TOOLSET / EVE_MCP_READONLY / EVE_MCP_EXCLUDE",
        "Adjust env filters or call eve_status; use EVE_MCP_TOOLSET=all to advertise every tool", false,
        {"eve_status"});
    out->set("filtered", true);
    out->set("tool", std::string(name));
    Poco::JSON::Array::Ptr tags = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    for (const auto& tag : mcpToolTags(name)) tags->add(tag);
    out->set("tags", tags);
    out->set("mutates", mcpToolMutates(name));
    return mcpStringify(Poco::Dynamic::Var(out));
}

}  // namespace eve::dev
