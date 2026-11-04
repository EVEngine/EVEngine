#include "devtools/McpEntityInspection.hpp"
#include <Poco/Base64Encoder.h>
#include <future>
#include <set>
#include "common/Capability.h"
#include "common/EntityInspection.h"
#include "devtools/FrameCapturePreview.hpp"
#include "devtools/McpJson.hpp"
#include "devtools/McpToolFamilies.hpp"

namespace eve::dev {
namespace {
using Object = Poco::JSON::Object;
using Array  = Poco::JSON::Array;
std::future<std::string> encoding;
std::string              encodingToken, encoded;
Object::Ptr              metadata(std::string_view phase) {
    Object::Ptr value = new Object;
    value->set("schemaId", "eve.entityViews");
    value->set("schemaVersion", 1);
    value->set("status", std::string(phase));
    return value;
}
std::string error(std::string message) {
    auto value = metadata("failed");
    value->set("message", std::move(message));
    return textContentResult(mcpStringify(value), true);
}
std::string pending(std::string_view phase, std::string_view token) {
    auto value = metadata(phase);
    value->set("token", std::string(token));
    return textContentResult(mcpStringify(value));
}
bool workerBusy() {
    return encoding.valid() && encoding.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}
bool integer(Object::Ptr args, const char* key, uint32_t& output) {
    if (!args->has(key)) return false;
    const auto value = args->get(key);
    if (!value.isInteger()) return false;
    const double number = value.convert<double>();
    if (number < 0 || number > UINT32_MAX) return false;
    output = value.convert<uint32_t>();
    return true;
}
}  // namespace

std::string callMcpEntityInspectionTool(std::string_view name, Object::Ptr args) try {
    if (name != "eve_entity_views" && name != "eve_entity_views_status" && name != "eve_entity_views_release")
        return error("Unknown entity inspection tool");
    if (!args) return error("Arguments are required");
    const bool                  request = name == "eve_entity_views";
    const std::set<std::string> allowed =
        request ? std::set<std::string>{"domain", "entityId", "generation", "mode", "cellSize"}
                : std::set<std::string>{"token"};
    for (const auto& [key, value] : *args) {
        (void)value;
        if (!allowed.contains(key)) return error("Unknown argument: " + key);
    }
    auto* provider = cap::query<IEntityInspection>();
    if (!provider)
        return error("Entity inspection provider unavailable; requires a running supported graphics backend");
    if (request) {
        EntityViewRequest input;
        if (!args->has("domain") || !args->get("domain").isString() ||
            args->getValue<std::string>("domain") != "renderable3d" || !args->has("mode") ||
            !args->get("mode").isString() || args->getValue<std::string>("mode") != "geometry" ||
            !integer(args, "entityId", input.entityId) || !integer(args, "generation", input.generation) ||
            (args->has("cellSize") && !integer(args, "cellSize", input.cellSize)) || input.cellSize < 64 ||
            input.cellSize > 1024)
            return error("Expected domain=renderable3d, mode=geometry, uint32 entityId/generation, cellSize=64..1024");
        if (workerBusy()) return error("Image encoder busy; poll or release the previous request");
        auto result = provider->request(input);
        if (!result.ok()) return error(result.status().describe());
        encoding = {};
        encodingToken.clear();
        encoded.clear();
        return pending("queued", result.value());
    }
    if (!args->has("token") || !args->get("token").isString()) return error("token must be a string");
    const auto token = args->getValue<std::string>("token");
    if (token.empty() || token.size() > 128) return error("Invalid token length");
    if (name == "eve_entity_views_release") {
        auto result = provider->release(token);
        if (!result.ok()) return error(result.status().describe());
        return pending(frameCapturePhaseName(result.value()), token);
    }
    // Validate provider token before consulting cached encoding; cancelled/restarted jobs cannot publish stale images.
    auto result = provider->snapshot(token);
    if (!result.ok()) return error(result.status().describe());
    const auto& snapshot = result.value();
    if (snapshot.capture.phase == FrameCapturePhase::Failed) return error(snapshot.capture.reason);
    if (snapshot.capture.phase != FrameCapturePhase::Ready)
        return pending(frameCapturePhaseName(snapshot.capture.phase), token);
    if (encodingToken == token) {
        if (!encoded.empty()) return encoded;
        if (workerBusy()) return pending("encoding", token);
        encoded = encoding.get();
        return encoded;
    }
    if (workerBusy()) return error("Image encoder busy");
    if (snapshot.capture.buffers.size() != 1 || !snapshot.capture.buffers[0].bytes)
        return error("Incomplete inspection snapshot");
    auto info = metadata("ready");
    info->set("token", token);
    info->set("mode", "geometry");
    info->set("scope", "isolated-entity");
    info->set("surfaceEncoding", "world-normal-rgb");
    info->set("gameMaterialsIncluded", false);
    info->set("sameFrozenGeometry", true);
    info->set("worldUnitsPerPixel", snapshot.worldUnitsPerPixel);
    info->set("entityId", snapshot.request.entityId);
    info->set("generation", snapshot.request.generation);
    info->set("renderFrameId", static_cast<Poco::UInt64>(snapshot.capture.frameId));
    info->set("width", snapshot.capture.buffers[0].width);
    info->set("height", snapshot.capture.buffers[0].height);
    info->set("matrixConvention", "column-major RH zero-to-one depth Y-down");
    Array::Ptr bounds = new Array;
    for (float number : snapshot.bounds) bounds->add(number);
    info->set("worldBounds", bounds);
    Array::Ptr                       views = new Array;
    const std::array<const char*, 4> names{"front +Z", "right +X", "top +Y up -Z", "game camera"};
    for (size_t i = 0; i < 4; ++i) {
        Object::Ptr view   = new Object;
        const auto& camera = snapshot.cameras[i];
        view->set("name", names[i]);
        view->set("x", camera.x);
        view->set("y", camera.y);
        view->set("width", camera.width);
        view->set("height", camera.height);
        Array::Ptr v = new Array, p = new Array;
        for (float n : camera.view) v->add(n);
        for (float n : camera.projection) p->add(n);
        view->set("view", v);
        view->set("projection", p);
        views->add(view);
    }
    info->set("views", views);
    const auto text = mcpStringify(info);
    encodingToken   = token;
    encoded.clear();
    encoding = std::async(std::launch::async, [buffer = snapshot.capture.buffers[0], text] {
        try {
            FramePreviewOptions options;
            options.maxWidth = options.maxHeight = 2048;
            auto image                           = frameCapturePreview(buffer, options);
            if (!image.ok()) return error(image.status().describe());
            std::ostringstream  stream;
            Poco::Base64Encoder encoder(stream);
            encoder.rdbuf()->setLineLength(0);
            encoder.write(reinterpret_cast<const char*>(image.value().png.data()),
                          std::streamsize(image.value().png.size()));
            encoder.close();
            return imageContentResult(stream.str(), "image/png", text);
        } catch (const std::exception& exception) {
            return error(exception.what());
        }
    });
    return pending("encoding", token);
} catch (const std::exception& exception) {
    return error(exception.what());
}

std::string_view mcpEntityInspectionSchemas() {
    return R"JSON(
{"name":"eve_entity_views","description":"Queue four isolated geometry diagnostic views of a generation-qualified Renderable3D: front/right/top plus original active camera, same frozen geometry. World-normal colors, no materials/lighting/scene occluders. Static, already-baked CPU morph and current four-influence skin poses supported; custom shaders/LOD/instances unsupported. Skin palettes must be affine, with valid joints and nonnegative weights. Returns token; poll eve_entity_views_status. Does not advance gameplay or change the camera.","inputSchema":{"type":"object","additionalProperties":false,"required":["domain","entityId","generation","mode"],"properties":{"domain":{"type":"string","enum":["renderable3d"]},"mode":{"type":"string","enum":["geometry"]},"entityId":{"type":"integer","minimum":0,"maximum":4294967295},"generation":{"type":"integer","minimum":0,"maximum":4294967295},"cellSize":{"type":"integer","minimum":64,"maximum":1024,"default":512}}}},
{"name":"eve_entity_views_status","description":"Poll entity view token. Ready returns a PNG four-view atlas and source camera/bounds metadata; GPU readback and PNG encoding never block this call. Layout: front/right on top, top/game camera below.","inputSchema":{"type":"object","additionalProperties":false,"required":["token"],"properties":{"token":{"type":"string","minLength":1,"maxLength":128}}}},
{"name":"eve_entity_views_release","description":"Release or cancel entity inspection; in-flight GPU resources retire after completion. Release before requesting another target. Ready snapshots expire after 60 seconds.","inputSchema":{"type":"object","additionalProperties":false,"required":["token"],"properties":{"token":{"type":"string","minLength":1,"maxLength":128}}}}
)JSON";
}
EVE_MCP_TOOL_FAMILY(EntityInspection, mcpEntityInspectionSchemas, callMcpEntityInspectionTool, ContentEnvelope)
}  // namespace eve::dev
