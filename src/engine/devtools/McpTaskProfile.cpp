#include "devtools/McpTaskProfile.hpp"

#include "devtools/McpToolFilter.hpp"

#include <cstdlib>
#include <mutex>
#include <unordered_set>

namespace eve::dev {
namespace {

std::mutex     g_mu;
std::string    g_processProfile;
std::string    g_sessionProfile;
McpFilterStats g_stats;
bool           g_processLoaded = false;

void ensureProcessLoadedUnlocked() {
    if (g_processLoaded) return;
    if (const char* raw = std::getenv("EVE_MCP_PROFILE")) {
        g_processProfile = raw;
    }
    g_processLoaded = true;
}

bool isKnownTaskKind(std::string_view kind) {
    return kind == "content" || kind == "script" || kind == "verify" || kind == "fix" || kind == "workshop";
}

bool isKnownProfile(std::string_view profile) {
    return profile == "content.scene" || profile == "content.map" || profile == "script.bind" ||
           profile == "verify.play" || profile == "fix.compile" || profile == "workshop.rpg";
}

std::string defaultProfileForKind(std::string_view kind) {
    if (kind == "content") return "content.scene";
    if (kind == "script") return "script.bind";
    if (kind == "fix") return "fix.compile";
    if (kind == "workshop") return "workshop.rpg";
    return "verify.play";
}

std::string taskKindForProfile(std::string_view profile) {
    if (profile.starts_with("content.")) return "content";
    if (profile.starts_with("script.")) return "script";
    if (profile.starts_with("fix.")) return "fix";
    if (profile.starts_with("workshop.")) return "workshop";
    return "verify";
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

bool isDangerEvalTool(std::string_view tool) { return tool == "eve_eval" || tool == "eve_run_script"; }

bool isAgentSessionTool(std::string_view tool) { return startsWith(tool, "eve_agent_"); }

bool isCoreDiscoverTool(std::string_view tool) {
    return equalsAny(tool, {"eve_status", "eve_game_world", "eve_ai_log", "eve_ai_note", "eve_crash_report",
                            "eve_console_read", "eve_console_write", "eve_console_clear"});
}

bool tagsAllow(std::string_view tool, const std::unordered_set<std::string>& tags) {
    for (const auto& tag : mcpToolTags(tool)) {
        if (tags.count(tag) != 0) return true;
    }
    return false;
}

}  // namespace

bool mcpResolveTaskProfile(std::string_view taskKind, std::string_view profile, std::string* outProfile,
                           std::string* outTaskKind, std::string* errorCode, std::string* errorMessage) {
    std::string kind = std::string(taskKind);
    std::string prof = std::string(profile);
    if (kind.empty() && prof.empty()) {
        kind = "verify";
        prof = "verify.play";
    } else if (kind.empty()) {
        if (!isKnownProfile(prof)) {
            if (errorCode) *errorCode = "invalid-profile";
            if (errorMessage) *errorMessage = "profile must be a known task profile id";
            return false;
        }
        kind = taskKindForProfile(prof);
    } else if (prof.empty()) {
        if (!isKnownTaskKind(kind)) {
            if (errorCode) *errorCode = "invalid-task-kind";
            if (errorMessage) *errorMessage = "taskKind must be content, script, verify, fix, or workshop";
            return false;
        }
        prof = defaultProfileForKind(kind);
    } else {
        if (!isKnownTaskKind(kind)) {
            if (errorCode) *errorCode = "invalid-task-kind";
            if (errorMessage) *errorMessage = "taskKind must be content, script, verify, fix, or workshop";
            return false;
        }
        if (!isKnownProfile(prof)) {
            if (errorCode) *errorCode = "invalid-profile";
            if (errorMessage) *errorMessage = "profile must be a known task profile id";
            return false;
        }
        if (taskKindForProfile(prof) != kind) {
            if (errorCode) *errorCode = "profile-task-mismatch";
            if (errorMessage) *errorMessage = "profile does not belong to the given taskKind";
            return false;
        }
    }
    if (outProfile) *outProfile = std::move(prof);
    if (outTaskKind) *outTaskKind = std::move(kind);
    return true;
}

std::vector<std::string> mcpProfileToolsetTags(std::string_view profile) {
    if (profile == "content.scene") return {"agent", "core", "scene"};
    if (profile == "content.map") return {"agent", "core", "editor"};
    if (profile == "script.bind") return {"agent", "core", "debug"};
    if (profile == "verify.play") return {"agent", "core", "play", "input"};
    if (profile == "fix.compile") return {"agent", "core", "debug"};
    if (profile == "workshop.rpg") return {"agent", "core", "play", "input", "editor", "scene"};
    return {};
}

std::string mcpProfileToolsetEnv(std::string_view profile) {
    const auto  tags = mcpProfileToolsetTags(profile);
    std::string out;
    for (const auto& tag : tags) {
        if (!out.empty()) out += ',';
        out += tag;
    }
    return out;
}

bool mcpProfileAllowsTool(std::string_view profile, std::string_view tool) {
    if (profile.empty()) return true;
    if (!isKnownProfile(profile)) return false;
    // Session / agent tools stay reachable so the Agent can escalate, status, and complete.
    if (isAgentSessionTool(tool) || tool == "eve_status" || tool == "eve_game_world") return true;

    if (isDangerEvalTool(tool)) {
        // Only the fix profile may touch eval/run_script; still not valid required evidence.
        return profile == "fix.compile";
    }
    // Stable facade: never leak into content/verify via shared scene/play tags.
    if (tool == "eve_workshop") return profile == "workshop.rpg";
    // Recipes carry the discovery tag too; it must not grant mutation to
    // verification or binding profiles through their shared agent toolset.
    if (tool == "eve_recipe_apply") return profile == "content.scene" || profile == "workshop.rpg";

    const auto                      tagList = mcpProfileToolsetTags(profile);
    std::unordered_set<std::string> tags(tagList.begin(), tagList.end());

    if (profile == "script.bind") {
        if (equalsAny(tool, {"eve_api_search", "eve_api_get", "eve_console_read", "eve_console_write",
                             "eve_console_clear", "eve_error_slice", "eve_crash_report"}))
            return true;
        // Keep debug observations, but not breakpoints / snapshots / eval (eval already rejected).
        if (equalsAny(tool, {"eve_stack", "eve_locals", "eve_list_breakpoints"})) return true;
        return false;
    }

    if (profile == "verify.play") {
        if (equalsAny(tool, {"eve_screenshot", "eve_screenshot_image", "eve_render_status", "eve_console_read"}))
            return true;
        return tagsAllow(tool, tags);
    }

    if (profile == "fix.compile") {
        if (isCoreDiscoverTool(tool) || tool == "eve_restart" || tool == "eve_error_slice" ||
            tool == "eve_api_search" || tool == "eve_api_get")
            return true;
        return tagsAllow(tool, tags);
    }

    if (profile == "content.scene") {
        if (startsWith(tool, "eve_procgen_") || startsWith(tool, "eve_recipe_") || tool == "eve_scene_info" ||
            tool == "eve_scene_director_status")
            return true;
        return tagsAllow(tool, tags);
    }

    if (profile == "content.map") {
        if (tool == "eve_console_read") return true;
        return tagsAllow(tool, tags);
    }

    if (profile == "workshop.rpg") {
        if (tool == "eve_workshop" || tool == "eve_screenshot" || tool == "eve_screenshot_image" ||
            tool == "eve_console_read" || tool == "eve_api_search" || tool == "eve_api_get")
            return true;
        return tagsAllow(tool, tags);
    }

    return tagsAllow(tool, tags);
}

std::optional<McpProfileEscalate> mcpProfileEscalateHint(std::string_view profile, std::string_view tool) {
    if (profile.empty() || mcpProfileAllowsTool(profile, tool)) return std::nullopt;

    McpProfileEscalate hint;
    hint.errorCode = "profile-denied";

    if (isDangerEvalTool(tool) && profile != "fix.compile") {
        hint.to     = "fix.compile";
        hint.reason = "eval and run_script are limited to fix.compile and cannot satisfy required criteria";
        return hint;
    }
    if ((startsWith(tool, "eve_scene_") || startsWith(tool, "eve_camera_") || startsWith(tool, "eve_procgen_") ||
         startsWith(tool, "eve_recipe_") || tool == "eve_whitebox_playtest") &&
        (profile == "verify.play" || profile == "script.bind" || profile == "fix.compile")) {
        hint.to     = "content.scene";
        hint.reason = "scene, recipe, or procgen mutation requires content.scene";
        return hint;
    }
    if (tool == "eve_workshop" && profile != "workshop.rpg") {
        hint.to     = "workshop.rpg";
        hint.reason = "workshop facade is limited to workshop.rpg";
        return hint;
    }
    if ((startsWith(tool, "eve_editor_") || startsWith(tool, "eve_host_")) &&
        (profile == "verify.play" || profile == "script.bind" || profile == "content.scene")) {
        hint.to     = profile == "content.scene" ? "content.map" : "workshop.rpg";
        hint.reason = "editor/host tools require content.map or workshop.rpg";
        return hint;
    }
    if ((tool == "eve_play" || tool == "eve_gameplay" || startsWith(tool, "eve_input_") ||
         startsWith(tool, "eve_ui_")) &&
        (profile == "content.scene" || profile == "content.map" || profile == "script.bind" ||
         profile == "fix.compile")) {
        hint.to     = "verify.play";
        hint.reason = "play/observe verification requires verify.play";
        return hint;
    }
    if ((tool == "eve_api_search" || tool == "eve_api_get") &&
        (profile == "content.scene" || profile == "content.map" || profile == "verify.play")) {
        hint.to     = "script.bind";
        hint.reason = "binding-contract lookup is opened by script.bind";
        return hint;
    }
    if ((tool == "eve_restart" || tool == "eve_error_slice") && profile != "fix.compile") {
        hint.to     = "fix.compile";
        hint.reason = "compile/runtime repair tools require fix.compile";
        return hint;
    }
    return std::nullopt;
}

void mcpSetProcessProfile(std::string_view profile) {
    std::lock_guard lock(g_mu);
    g_processProfile = std::string(profile);
    g_processLoaded  = true;
}

std::string mcpProcessProfile() {
    std::lock_guard lock(g_mu);
    ensureProcessLoadedUnlocked();
    return g_processProfile;
}

void mcpSetSessionProfile(std::string_view profile) {
    std::lock_guard lock(g_mu);
    g_sessionProfile = std::string(profile);
}

std::string mcpSessionProfile() {
    std::lock_guard lock(g_mu);
    return g_sessionProfile;
}

void mcpFilterNoteCall(bool allowed, std::size_t resultBytes, bool offeredEscalate) {
    std::lock_guard lock(g_mu);
    ++g_stats.toolCalls;
    if (!allowed) ++g_stats.toolsFiltered;
    if (offeredEscalate) ++g_stats.escalateCount;
    g_stats.approxResultBytes += static_cast<std::uint64_t>(resultBytes);
}

McpFilterStats mcpFilterStats() {
    std::lock_guard lock(g_mu);
    return g_stats;
}

void mcpFilterStatsResetForTest() {
    std::lock_guard lock(g_mu);
    g_stats = {};
}

}  // namespace eve::dev
