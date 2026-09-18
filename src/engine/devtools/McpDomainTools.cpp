#include "devtools/McpDomainTools.hpp"

#include "devtools/McpArgs.hpp"
#include "devtools/McpJson.hpp"

#include "common/Capability.h"
#include "common/DecalQuery.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Object.h>

#include <algorithm>
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

}  // namespace

bool isMcpDomainTool(std::string_view name) {
    return name == "eve_decal_status" || name == "eve_decal_project" || name == "eve_decal_remove" ||
           name == "eve_decal_clear" || name == "eve_decal_set_limit";
}

std::string callMcpDomainTool(std::string_view name, Poco::JSON::Object::Ptr args) {
    if (name == "eve_decal_status") return decalStatus();
    if (name == "eve_decal_project") return decalProject(args);
    if (name == "eve_decal_remove") return decalRemove(args);
    if (name == "eve_decal_clear") return decalClear();
    if (name == "eve_decal_set_limit") return decalSetLimit(args);
    return errorPayload("unknown domain tool '" + std::string(name) + "'");
}

std::string_view mcpDomainToolSchemas() {
    return R"json({"name":"eve_decal_status","description":"Live decal count. Decals are runtime surface projections (scorch marks, blood, footprints) owned by the decal module.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_decal_project","description":"Project a decal at a world position facing a normal. Omitted size/depth/lifetime/fade fields use the decal module defaults; no albedo texture is bound through this path, so decals take the module's default look.","inputSchema":{"type":"object","properties":{"x":{"type":"number"},"y":{"type":"number"},"z":{"type":"number"},"nx":{"type":"number"},"ny":{"type":"number"},"nz":{"type":"number"},"kind":{"type":"string","description":"decal kind; a kind with a configured limit evicts its oldest instance first"},"size":{"type":"number"},"depth":{"type":"number"},"randomYaw":{"type":"boolean"},"seed":{"type":"integer","description":"deterministic yaw seed"},"fadeIn":{"type":"number"},"lifetime":{"type":"number"},"fadeOut":{"type":"number"}},"required":["x","y","z"]}},{"name":"eve_decal_remove","description":"Remove one decal by id and report the remaining count.","inputSchema":{"type":"object","properties":{"id":{"type":"integer","minimum":1}},"required":["id"]}},{"name":"eve_decal_clear","description":"Remove every decal and report how many were dropped.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_decal_set_limit","description":"Set the per-kind instance limit used for eviction; 0 disables eviction for that kind.","inputSchema":{"type":"object","properties":{"kind":{"type":"string"},"limit":{"type":"integer","minimum":0}},"required":["kind","limit"]}})json";
}

}  // namespace eve::dev
