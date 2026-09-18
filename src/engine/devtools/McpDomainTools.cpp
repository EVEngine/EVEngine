#include "devtools/McpDomainTools.hpp"

#include "devtools/McpArgs.hpp"
#include "devtools/McpJson.hpp"

#include "common/CameraObstruction.h"
#include "common/Capability.h"
#include "common/DecalQuery.h"
#include "common/ProcgenWorldQuery.h"
#include "common/ProfilerQuery.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace eve::dev {
namespace {

std::string textPayload(Poco::JSON::Object::Ptr object, bool isError = false) {
    return textContentResult(mcpStringify(Poco::Dynamic::Var(object)), isError);
}

std::string errorPayload(const std::string& message) {
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", false);
    out->set("error", message);
    return textPayload(out, true);
}

/** Trimmed-out provider: report the missing capability by name, not silently. */
std::string unavailablePayload(const char* capability) {
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", false);
    out->set("unavailable", capability);
    out->set("error", std::string(capability) + " is not provided by the loaded modules");
    return textPayload(out);
}

// ============================== Decals =====================================

/** `[x,y,z]` argument, falling back to `def` when absent or malformed. */
std::array<float, 3> getArgVec3(Poco::JSON::Object::Ptr args, const char* key, const std::array<float, 3>& def) {
    if (!args || !args->has(key)) return def;
    try {
        auto array = args->getArray(key);
        if (array && array->size() >= 3) {
            return {static_cast<float>(array->get(0).convert<double>()),
                    static_cast<float>(array->get(1).convert<double>()),
                    static_cast<float>(array->get(2).convert<double>())};
        }
    } catch (...) {
    }
    return def;
}

/** Every collision category. A zero mask would match nothing, not everything. */
constexpr std::uint32_t kAllCategoryBits = 0xFFFFFFFFu;

std::string decalStatus() {
    auto* decals = eve::cap::query<eve::IDecalQuery>();
    if (!decals) return unavailablePayload(eve::IDecalQuery::capabilityName);
    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("count", decals->count());
    return textPayload(out);
}

std::string decalProject(Poco::JSON::Object::Ptr args) {
    auto* decals = eve::cap::query<eve::IDecalQuery>();
    if (!decals) return unavailablePayload(eve::IDecalQuery::capabilityName);

    // A decal needs a surface normal; up is the least surprising default, and a
    // caller that omits it almost always means "on the ground".
    const float nx = getArgFloat(args, "nx", 0.f);
    const float ny = getArgFloat(args, "ny", 1.f);
    const float nz = getArgFloat(args, "nz", 0.f);

    // Zero means "engine default" for every size/time field: the decal module
    // owns those defaults, so the tool must not invent a second set.
    const int id = decals->project(getArgFloat(args, "x"), getArgFloat(args, "y"), getArgFloat(args, "z"), nx, ny, nz,
                                   /*albedoTexture=*/nullptr, getArgString(args, "kind"), getArgFloat(args, "size"),
                                   getArgFloat(args, "depth"), getArgBool(args, "randomYaw"), getArgInt(args, "seed"),
                                   getArgFloat(args, "fadeIn"), getArgFloat(args, "lifetime"), getArgFloat(args, "fadeOut"));
    if (id <= 0) return errorPayload("decal projection was rejected");

    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("id", id);
    out->set("count", decals->count());
    return textPayload(out);
}

std::string decalRemove(Poco::JSON::Object::Ptr args) {
    auto* decals = eve::cap::query<eve::IDecalQuery>();
    if (!decals) return unavailablePayload(eve::IDecalQuery::capabilityName);
    const int id = getArgInt(args, "id", -1);
    if (id <= 0) return errorPayload("missing decal id");
    if (!decals->remove(id)) return errorPayload("no decal with id " + std::to_string(id));

    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("count", decals->count());
    return textPayload(out);
}

std::string decalClear() {
    auto* decals = eve::cap::query<eve::IDecalQuery>();
    if (!decals) return unavailablePayload(eve::IDecalQuery::capabilityName);
    const int before = decals->count();
    decals->clearAll();

    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("removed", before);
    out->set("count", decals->count());
    return textPayload(out);
}

std::string decalSetLimit(Poco::JSON::Object::Ptr args) {
    auto* decals = eve::cap::query<eve::IDecalQuery>();
    if (!decals) return unavailablePayload(eve::IDecalQuery::capabilityName);
    const std::string kind = getArgString(args, "kind");
    if (kind.empty()) return errorPayload("missing kind");
    const int limit = getArgInt(args, "limit", -1);
    if (limit < 0) return errorPayload("limit must be >= 0");
    decals->setLimit(kind, limit);

    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("kind", kind);
    out->set("limit", limit);
    return textPayload(out);
}

// ====================== 3D spatial queries ==================================
// The 2D tools (eve_physics_raycast) cannot answer "what is between the camera
// and the player" or "how high is the ground here" in a 3D scene; these two
// capabilities already existed and only lacked an agent surface.

std::string physicsSphereCast(Poco::JSON::Object::Ptr args) {
    auto* cast = eve::cap::query<eve::ICameraObstructionQuery>();
    if (!cast) return unavailablePayload(eve::ICameraObstructionQuery::capabilityName);
    if (!args || !args->has("from") || !args->has("to")) return errorPayload("from and to are required");

    const std::array<float, 3> from = getArgVec3(args, "from", {0.f, 0.f, 0.f});
    const std::array<float, 3> to   = getArgVec3(args, "to", {0.f, 0.f, 0.f});
    const float                radius = getArgFloat(args, "radius", 0.f);
    const long long            mask   = getArgInt64(args, "maskBits", static_cast<long long>(kAllCategoryBits));

    eve::CameraObstructionHit hit;
    const bool                swept =
        cast->sphereCast(from[0], from[1], from[2], to[0], to[1], to[2], radius,
                         static_cast<std::uint64_t>(mask), getArgInt(args, "ignoredBodyId", -1), &hit);
    if (!swept) return errorPayload("sphere cast is unavailable (no live 3D physics world)");

    Poco::JSON::Object::Ptr out = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("hit", hit.hit);
    out->set("radius", radius);
    if (hit.hit) {
        out->set("bodyId", hit.bodyId);
        out->set("fraction", hit.fraction);
        Poco::JSON::Array::Ptr point = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        point->add(hit.x);
        point->add(hit.y);
        point->add(hit.z);
        out->set("point", point);
        Poco::JSON::Array::Ptr normal = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        normal->add(hit.normalX);
        normal->add(hit.normalY);
        normal->add(hit.normalZ);
        out->set("normal", normal);
    }
    return textPayload(out);
}

std::string worldProjectDown(Poco::JSON::Object::Ptr args) {
    auto* world = eve::cap::query<eve::IProcgenWorldQuery>();
    if (!world) return unavailablePayload(eve::IProcgenWorldQuery::capabilityName);

    const float     x    = getArgFloat(args, "x");
    const float     z    = getArgFloat(args, "z");
    const float     maxY = getArgFloat(args, "maxY", 1000.f);
    const float     minY = getArgFloat(args, "minY", -1000.f);
    const long long mask = getArgInt64(args, "maskBits", static_cast<long long>(kAllCategoryBits));

    auto projected = world->projectDown(x, z, maxY, minY, static_cast<std::uint64_t>(mask));
    if (!projected) return errorPayload("world projection failed: " + projected.status().describe());

    const eve::ProcgenSurfaceHit& surface = projected.value();
    Poco::JSON::Object::Ptr       out     = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    out->set("ok", true);
    out->set("hit", surface.hit);
    out->set("x", x);
    out->set("z", z);
    if (surface.hit) {
        out->set("objectId", static_cast<Poco::Int64>(surface.objectId));
        out->set("y", surface.y);
        Poco::JSON::Array::Ptr normal = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
        normal->add(surface.normalX);
        normal->add(surface.normalY);
        normal->add(surface.normalZ);
        out->set("normal", normal);
    }
    return textPayload(out);
}

// ========================== Profiler ========================================

/** The profiler owns its document schema; the tool forwards it verbatim. */
std::string profilerFrame() {
    auto* profiler = eve::cap::query<eve::IProfilerQuery>();
    if (!profiler) return unavailablePayload(eve::IProfilerQuery::capabilityName);
    return textContentResult(profiler->frameJson());
}

std::string profilerReport() {
    auto* profiler = eve::cap::query<eve::IProfilerQuery>();
    if (!profiler) return unavailablePayload(eve::IProfilerQuery::capabilityName);
    const std::string report = profiler->textReport();
    return textContentResult(report.empty() ? "(no completed frame yet)\n" : report);
}

}  // namespace

bool isMcpDomainTool(std::string_view name) {
    return name == "eve_decal_status" || name == "eve_decal_project" || name == "eve_decal_remove" ||
           name == "eve_decal_clear" || name == "eve_decal_set_limit" || name == "eve_physics_sphere_cast" ||
           name == "eve_world_project_down" || name == "eve_profiler_frame" || name == "eve_profiler_report";
}

std::string callMcpDomainTool(std::string_view name, Poco::JSON::Object::Ptr args) {
    if (name == "eve_decal_status") return decalStatus();
    if (name == "eve_decal_project") return decalProject(args);
    if (name == "eve_decal_remove") return decalRemove(args);
    if (name == "eve_decal_clear") return decalClear();
    if (name == "eve_decal_set_limit") return decalSetLimit(args);
    if (name == "eve_physics_sphere_cast") return physicsSphereCast(args);
    if (name == "eve_world_project_down") return worldProjectDown(args);
    if (name == "eve_profiler_frame") return profilerFrame();
    if (name == "eve_profiler_report") return profilerReport();
    return errorPayload("unknown domain tool '" + std::string(name) + "'");
}

std::string_view mcpDomainToolSchemas() {
    return R"json({"name":"eve_decal_status","description":"Live decal count. Decals are runtime surface projections (scorch marks, blood, footprints) owned by the decal module.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_decal_project","description":"Project a decal at a world position facing a normal. Omitted size/depth/lifetime/fade fields use the decal module defaults; no albedo texture is bound through this path, so decals take the module's default look.","inputSchema":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"nx":{"type":"number"},"ny":{"type":"number"},"nz":{"type":"number"},"kind":{"type":"string","description":"decal kind; a kind with a configured limit evicts its oldest instance first"},"size":{"type":"number"},"depth":{"type":"number"},"randomYaw":{"type":"boolean"},"seed":{"type":"integer","description":"deterministic yaw seed"},"fadeIn":{"type":"number"},"lifetime":{"type":"number"},"fadeOut":{"type":"number"}},"required":["x","y","z"]}},{"name":"eve_decal_remove","description":"Remove one decal by id and report the remaining count.","inputSchema":{"type":"object","properties":{"id":{"type":"integer","minimum":1}},"required":["id"]}},{"name":"eve_decal_clear","description":"Remove every decal and report how many were dropped.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_decal_set_limit","description":"Set the per-kind instance limit used for eviction; 0 disables eviction for that kind.","inputSchema":{"type":"object","properties":{"kind":{"type":"string"},"limit":{"type":"integer","minimum":0}},"required":["kind","limit"]}},{"name":"eve_physics_sphere_cast","description":"Sweep a sphere (radius 0 = ray) through every registered 3D physics world and return the closest hit: bodyId, fraction along the segment, world point and surface normal. This is the 3D counterpart of eve_physics_raycast, which only queries 2D worlds.","inputSchema":{"type":"object","properties":{"from":{"type":"array","items":{"type":"number"},"description":"[x,y,z] segment start"},"to":{"type":"array","items":{"type":"number"},"description":"[x,y,z] segment end"},"radius":{"type":"number","minimum":0,"description":"sphere radius (default 0 = ray)"},"maskBits":{"type":"integer","description":"collision category mask; default matches every category, 0 matches none"},"ignoredBodyId":{"type":"integer","description":"body to skip, e.g. the camera rig"}},"required":["from","to"]}},{"name":"eve_world_project_down","description":"Project a vertical ray through the procedural/3D world surface at (x,z) and return the closest hit height and normal. Use it to place objects on terrain or to check ground level under a moving entity.","inputSchema":{"type":"object","properties":{"x":{"type":"number"},"z":{"type":"number"},"maxY":{"type":"number","description":"projection start height (default 1000)"},"minY":{"type":"number","description":"projection end height (default -1000)"},"maskBits":{"type":"integer","description":"collision category mask; default matches every category"}},"required":["x","z"]}},{"name":"eve_profiler_frame","description":"Last completed frame's profile as JSON (schema eve.profiler.frame): enabled/hasFrame, cpuFrameMs (sum of top-level zone self times), gpuMs when a GPU timer is available, and the per-module/per-zone self and total milliseconds sorted by self time. Use it for unattended performance work instead of guessing from wall-clock timings.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_profiler_report","description":"Human-readable per-module/per-zone hotspot report of the last completed frame (the same text the profiler panel shows).","inputSchema":{"type":"object","properties":{}}})json";
}

}  // namespace eve::dev
