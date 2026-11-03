#include "devtools/McpActionGuideTools.hpp"

#include "common/ActionGuideIngest.h"
#include "common/ActionGuidePack.h"
#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "devtools/ActionGuideExport.hpp"
#include "devtools/ActionGuideIngestIO.hpp"
#include "devtools/McpJson.hpp"
#include "devtools/McpServer.hpp"
#include "devtools/McpToolFamilies.hpp"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::dev {
namespace {

using Object = Poco::JSON::Object;
using Array  = Poco::JSON::Array;

struct GuidePackSession {
    ActionGuidePack pack;
    bool            present = false;
};

GuidePackSession& session() {
    static GuidePackSession value;
    return value;
}

std::string textPayload(Object::Ptr object, bool isError = false) {
    return textContentResult(mcpStringify(Poco::Dynamic::Var(object)), isError);
}

Object::Ptr base(std::string_view status) {
    Object::Ptr out = new Object;
    out->set("schemaId", "eve.action.guide-pack.mcp");
    out->set("schemaVersion", 1);
    out->set("ok", status == "ok" || status == "ready");
    out->set("status", std::string(status));
    return out;
}

std::string error(std::string message, std::string hint = {}) {
    auto out = base("failed");
    out->set("ok", false);
    out->set("error", std::move(message));
    if (!hint.empty()) out->set("hint", std::move(hint));
    return textPayload(out, true);
}

std::string resultError(const Status& status) {
    auto out = base("failed");
    out->set("ok", false);
    out->set("error", status.describe());
    if (status.hasDiagnostics() && status.primaryDiagnostic()) out->set("path", status.primaryDiagnostic()->path());
    return textPayload(out, true);
}

Object::Ptr parseObjectArg(Object::Ptr args, const char* key) {
    if (!args || !args->has(key)) return {};
    try {
        auto value = args->get(key);
        if (value.type() == typeid(Object::Ptr)) return value.extract<Object::Ptr>();
        if (value.isString()) {
            Poco::JSON::Parser parser;
            auto               root = parser.parse(value.extract<std::string>());
            if (root.type() == typeid(Object::Ptr)) return root.extract<Object::Ptr>();
        }
    } catch (...) {
    }
    return {};
}

Result<ActionGuidePack> packFromArgs(Object::Ptr args) {
    Object::Ptr packObject = parseObjectArg(args, "pack");
    if (!packObject) {
        return Result<ActionGuidePack>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "pack object or JSON string required", "pack"));
    }
    auto json   = mcpStringify(Poco::Dynamic::Var(packObject));
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<ActionGuidePack>::failure(parsed.status());
    return decodeActionGuidePack(parsed.value());
}

Object::Ptr packJsonObject(const ActionGuidePack& pack) {
    auto encoded = encodeActionGuidePack(pack);
    if (!encoded) return {};
    auto json = encoded.value().toJson();
    if (!json) return {};
    try {
        Poco::JSON::Parser parser;
        auto               root = parser.parse(json.value());
        if (root.type() == typeid(Object::Ptr)) return root.extract<Object::Ptr>();
    } catch (...) {
    }
    return {};
}

std::string validateTool(Object::Ptr args) {
    auto pack = packFromArgs(args);
    if (!pack) return resultError(pack.status());
    auto encoded = encodeActionGuidePack(pack.value());
    if (!encoded) return resultError(encoded.status());
    auto out = base("ok");
    out->set("revision", static_cast<Poco::UInt64>(pack.value().revision));
    out->set("shotCount", static_cast<int>(pack.value().shots.size()));
    if (auto object = packJsonObject(pack.value())) out->set("pack", object);
    return textPayload(out);
}

std::string setTool(Object::Ptr args) {
    auto pack = packFromArgs(args);
    if (!pack) return resultError(pack.status());
    std::optional<std::uint64_t> expected;
    if (args && args->has("expectedRevision")) {
        try {
            const auto value = args->get("expectedRevision");
            if (!value.isInteger() && !value.isNumeric())
                return error("InvalidArguments", "expectedRevision must be an integer");
            const auto number = value.convert<std::int64_t>();
            if (number < 0) return error("InvalidArguments", "expectedRevision must be >= 0");
            expected = static_cast<std::uint64_t>(number);
        } catch (...) {
            return error("InvalidArguments", "expectedRevision must be an integer");
        }
    }
    auto& state = session();
    if (expected) {
        if (!state.present) return error("Conflict", "no session pack; omit expectedRevision on first set");
        if (state.pack.revision != *expected) {
            auto out = base("failed");
            out->set("ok", false);
            out->set("error", "Conflict");
            out->set("revision", static_cast<Poco::UInt64>(state.pack.revision));
            out->set("hint", "Pass expectedRevision matching the current session revision");
            return textPayload(out, true);
        }
    }
    state.pack    = std::move(pack).takeValue();
    state.present = true;
    auto out      = base("ok");
    out->set("revision", static_cast<Poco::UInt64>(state.pack.revision));
    if (auto object = packJsonObject(state.pack)) out->set("pack", object);
    return textPayload(out);
}

std::string getTool(Object::Ptr) {
    auto& state = session();
    if (!state.present) return error("NotFound", "Call eve_action_guide_pack_set first");
    auto out = base("ok");
    out->set("revision", static_cast<Poco::UInt64>(state.pack.revision));
    if (auto object = packJsonObject(state.pack)) out->set("pack", object);
    return textPayload(out);
}

std::string landmarksTool(Object::Ptr args) {
    Result<ActionGuidePack> packResult = [&]() -> Result<ActionGuidePack> {
        if (args && args->has("pack")) return packFromArgs(args);
        if (!session().present) {
            return Result<ActionGuidePack>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "pack or session required", "pack"));
        }
        return Result<ActionGuidePack>::success(session().pack);
    }();
    if (!packResult) return resultError(packResult.status());

    if (!args || !args->has("bones")) return error("InvalidArguments", "bones array is required");
    Array::Ptr bonesArg;
    try {
        bonesArg = args->getArray("bones");
    } catch (...) {
        return error("InvalidArguments", "bones must be an array");
    }
    if (!bonesArg) return error("InvalidArguments", "bones must be an array");

    std::vector<ActionGuideBoneWorld> bones;
    bones.reserve(bonesArg->size());
    for (unsigned i = 0; i < bonesArg->size(); ++i) {
        Object::Ptr boneObject;
        try {
            boneObject = bonesArg->getObject(i);
        } catch (...) {
            return error("InvalidArguments", "bones[" + std::to_string(i) + "] must be an object");
        }
        if (!boneObject || !boneObject->has("boneId") || !boneObject->has("position"))
            return error("InvalidArguments", "bones[" + std::to_string(i) + "] needs boneId and position");
        ActionGuideBoneWorld bone;
        try {
            bone.boneId = boneObject->getValue<std::string>("boneId");
            if (boneObject->has("name")) bone.name = boneObject->getValue<std::string>("name");
            if (boneObject->has("parentId")) bone.parentId = boneObject->getValue<std::string>("parentId");
            auto position = boneObject->getArray("position");
            if (!position || position->size() != 3)
                return error("InvalidArguments", "bones[" + std::to_string(i) + "].position must be [x,y,z]");
            for (unsigned c = 0; c < 3; ++c) bone.position[c] = position->getElement<double>(c);
        } catch (...) {
            return error("InvalidArguments", "bones[" + std::to_string(i) + "] has invalid fields");
        }
        bones.push_back(std::move(bone));
    }

    auto landmarks = projectActionGuideLandmarks(packResult.value(), bones);
    if (!landmarks) return resultError(landmarks.status());

    Array::Ptr items = new Array;
    for (const auto& landmark : landmarks.value()) {
        Object::Ptr item = new Object;
        item->set("boneId", landmark.boneId);
        item->set("name", landmark.name);
        item->set("x", landmark.x);
        item->set("y", landmark.y);
        item->set("depth", landmark.depth);
        if (landmark.parentId) item->set("parentId", *landmark.parentId);
        items->add(item);
    }
    auto out = base("ok");
    out->set("revision", static_cast<Poco::UInt64>(packResult.value().revision));
    out->set("landmarks", items);
    out->set("canvasWidth", static_cast<int>(packResult.value().canvas.width));
    out->set("canvasHeight", static_cast<int>(packResult.value().canvas.height));
    return textPayload(out);
}

IActionGuideCapture* requireProvider() { return cap::query<IActionGuideCapture>(); }

Result<ActionGuidePack> resolvePack(Object::Ptr args) {
    if (args && args->has("pack")) return packFromArgs(args);
    if (!session().present) {
        return Result<ActionGuidePack>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "pack or session required", "pack"));
    }
    return Result<ActionGuidePack>::success(session().pack);
}

std::vector<std::string> parseShotIds(Object::Ptr args) {
    std::vector<std::string> shotIds;
    if (!args || !args->has("shotIds")) return shotIds;
    try {
        auto array = args->getArray("shotIds");
        if (!array) return shotIds;
        shotIds.reserve(array->size());
        for (unsigned i = 0; i < array->size(); ++i) shotIds.push_back(array->getElement<std::string>(i));
    } catch (...) {
        shotIds.clear();
        shotIds.push_back("__invalid__");
    }
    return shotIds;
}

std::string captureTool(Object::Ptr args) {
    auto* provider = requireProvider();
    if (!provider) {
        return error("ProviderAbsent",
                     "IActionGuideCapture is not registered in this build; validate/landmarks work without GPU");
    }
    auto pack = resolvePack(args);
    if (!pack) return resultError(pack.status());
    auto encoded = encodeActionGuidePack(pack.value());
    if (!encoded) return resultError(encoded.status());
    auto json = encoded.value().toJson();
    if (!json) return resultError(json.status());
    auto shotIds = parseShotIds(args);
    if (shotIds.size() == 1 && shotIds[0] == "__invalid__")
        return error("InvalidArguments", "shotIds must be an array of strings");
    auto token = provider->requestCapture(json.value(), shotIds);
    if (!token) return resultError(token.status());
    auto out = base("queued");
    out->set("ok", true);
    out->set("token", token.value());
    out->set("revision", static_cast<Poco::UInt64>(pack.value().revision));
    return textPayload(out);
}

std::string statusTool(Object::Ptr args) {
    auto* provider = requireProvider();
    if (!provider) {
        return error("ProviderAbsent",
                     "IActionGuideCapture is not registered in this build; validate/landmarks work without GPU");
    }
    if (!args || !args->has("token")) return error("InvalidArguments", "token is required");
    std::string token;
    try {
        token = args->getValue<std::string>("token");
    } catch (...) {
        return error("InvalidArguments", "token must be a string");
    }
    auto snapshot = provider->captureSnapshot(token);
    if (!snapshot) return resultError(snapshot.status());
    const auto& snap = snapshot.value();
    auto        out  = base(actionGuideCapturePhaseName(snap.phase));
    out->set("ok", snap.phase == ActionGuideCapturePhase::Ready || snap.phase == ActionGuideCapturePhase::Queued ||
                       snap.phase == ActionGuideCapturePhase::Rendering);
    out->set("token", snap.token);
    out->set("phase", std::string(actionGuideCapturePhaseName(snap.phase)));
    out->set("revision", static_cast<Poco::UInt64>(snap.revision));
    out->set("backend", snap.backend);
    out->set("shotCount", static_cast<int>(snap.shots.size()));
    if (!snap.reason.empty()) out->set("reason", snap.reason);
    if (snap.phase == ActionGuideCapturePhase::Ready) {
        Array::Ptr shots = new Array;
        for (const auto& shot : snap.shots) {
            Object::Ptr item = new Object;
            item->set("shotId", shot.shotId);
            Array::Ptr aovs = new Array;
            for (const auto& aov : shot.aovs) aovs->add(aov.aov);
            item->set("aovs", aovs);
            shots->add(item);
        }
        out->set("shots", shots);
    }
    return textPayload(out);
}

std::string exportTool(Object::Ptr args) {
    auto* provider = requireProvider();
    if (!provider) {
        return error("ProviderAbsent",
                     "IActionGuideCapture is not registered in this build; validate/landmarks work without GPU");
    }
    if (!args || !args->has("token") || !args->has("path"))
        return error("InvalidArguments", "token and path are required");
    std::string token;
    std::string path;
    try {
        token = args->getValue<std::string>("token");
        path  = args->getValue<std::string>("path");
    } catch (...) {
        return error("InvalidArguments", "token and path must be strings");
    }
    if (path.empty() || path.find('\0') != std::string::npos) return error("InvalidExportPath");
    std::filesystem::path resolved;
    try {
        resolved         = std::filesystem::path(path);
        const auto& root = McpServer::instance().gameRoot();
        if (resolved.is_relative() && !root.empty()) resolved = std::filesystem::path(root) / resolved;
        resolved = std::filesystem::absolute(resolved).lexically_normal();
    } catch (const std::exception&) {
        return error("InvalidExportPath");
    }
    auto snapshot = provider->captureSnapshot(token);
    if (!snapshot) return resultError(snapshot.status());
    if (snapshot.value().phase != ActionGuideCapturePhase::Ready)
        return error("NotReady", "Capture must reach ready before export");
    auto written = writeActionGuideArtifactPack(snapshot.value(), resolved);
    if (!written) return resultError(written.status());
    auto summaryJson = written.value().toJson();
    if (!summaryJson) return resultError(summaryJson.status());
    auto out = base("ready");
    out->set("ok", true);
    out->set("token", token);
    out->set("exportRoot", resolved.generic_string());
    try {
        Poco::JSON::Parser parser;
        auto               summary = parser.parse(summaryJson.value()).extract<Object::Ptr>();
        out->set("artifact", summary);
    } catch (...) {
        out->set("hint", "export succeeded but artifact summary JSON could not be re-parsed");
    }
    return textPayload(out);
}

std::filesystem::path resolvePathArg(const std::string& path) {
    auto        resolved = std::filesystem::path(path);
    const auto& root     = McpServer::instance().gameRoot();
    if (resolved.is_relative() && !root.empty()) resolved = std::filesystem::path(root) / resolved;
    return std::filesystem::absolute(resolved).lexically_normal();
}

std::string ingestTool(Object::Ptr args) {
    if (!args || !args->has("candidateDir") || !args->has("outputDir"))
        return error("InvalidArguments", "candidateDir and outputDir are required");
    std::string candidateDir;
    std::string outputDir;
    try {
        candidateDir = args->getValue<std::string>("candidateDir");
        outputDir    = args->getValue<std::string>("outputDir");
    } catch (...) {
        return error("InvalidArguments", "candidateDir and outputDir must be strings");
    }
    if (candidateDir.empty() || outputDir.empty() || candidateDir.find('\0') != std::string::npos ||
        outputDir.find('\0') != std::string::npos)
        return error("InvalidExportPath");
    if (args->has("commitAbility")) {
        try {
            if (args->getValue<bool>("commitAbility"))
                return error("Unsupported",
                             "commitAbility=true lands after verify; ingest writes ability.draft.json only");
        } catch (...) {
            return error("InvalidArguments", "commitAbility must be a boolean");
        }
    }

    auto pack = resolvePack(args);
    if (!pack) return resultError(pack.status());
    if (args->has("expectedRevision")) {
        try {
            const auto expected = args->get("expectedRevision").convert<std::int64_t>();
            if (expected < 0) return error("InvalidArguments", "expectedRevision must be >= 0");
            if (static_cast<std::uint64_t>(expected) != pack.value().revision)
                return error("Conflict", "expectedRevision does not match pack.revision");
        } catch (...) {
            return error("InvalidArguments", "expectedRevision must be an integer");
        }
    }

    std::filesystem::path candidatePath;
    std::filesystem::path outputPath;
    try {
        candidatePath = resolvePathArg(candidateDir);
        outputPath    = resolvePathArg(outputDir);
    } catch (const std::exception&) {
        return error("InvalidExportPath");
    }

    auto frames = loadActionGuideCandidates(candidatePath, pack.value());
    if (!frames) return resultError(frames.status());

    ActionGuideIngestRequest request;
    request.pack                   = pack.value();
    request.frames                 = std::move(frames).takeValue();
    request.requireTransparentEdge = pack.value().canvas.transparent;
    if (args->has("clipName")) {
        try {
            request.outputClipName = args->getValue<std::string>("clipName");
        } catch (...) {
            return error("InvalidArguments", "clipName must be a string");
        }
    }

    auto draft = buildActionGuideIngestDraft(request);
    if (!draft) return resultError(draft.status());
    auto written = writeActionGuideIngestPack(draft.value(), request.frames, outputPath);
    if (!written) return resultError(written.status());
    auto summaryJson = written.value().toJson();
    if (!summaryJson) return resultError(summaryJson.status());

    auto out = base("ready");
    out->set("ok", true);
    out->set("sourceGuideRevision", static_cast<Poco::UInt64>(draft.value().sourceGuideRevision));
    out->set("frameCount", static_cast<int>(draft.value().frameCount));
    out->set("clipName", draft.value().clipName);
    out->set("sequencePattern", draft.value().sequencePattern);
    out->set("sequenceFirst", static_cast<Poco::Int64>(draft.value().sequenceFirst));
    out->set("sequenceLast", static_cast<Poco::Int64>(draft.value().sequenceLast));
    out->set("exportRoot", outputPath.generic_string());
    try {
        Poco::JSON::Parser parser;
        out->set("artifact", parser.parse(summaryJson.value()).extract<Object::Ptr>());
        auto abilityJson = draft.value().abilityDraft.toJson();
        if (abilityJson) out->set("abilityDraft", parser.parse(abilityJson.value()).extract<Object::Ptr>());
    } catch (...) {
        out->set("hint", "ingest succeeded but summary JSON could not be re-parsed");
    }
    return textPayload(out);
}

}  // namespace

bool isMcpActionGuideTool(std::string_view name) {
    return name == "eve_action_guide_pack_validate" || name == "eve_action_guide_pack_get" ||
           name == "eve_action_guide_pack_set" || name == "eve_action_guide_landmarks" ||
           name == "eve_action_guide_capture" || name == "eve_action_guide_status" ||
           name == "eve_action_guide_export" || name == "eve_action_guide_ingest" || name == "eve_action_guide_verify";
}

void resetMcpActionGuide() { session() = {}; }

std::string callMcpActionGuideTool(std::string_view name, Object::Ptr args) {
    if (name == "eve_action_guide_pack_validate") return validateTool(args);
    if (name == "eve_action_guide_pack_get") return getTool(args);
    if (name == "eve_action_guide_pack_set") return setTool(args);
    if (name == "eve_action_guide_landmarks") return landmarksTool(args);
    if (name == "eve_action_guide_capture") return captureTool(args);
    if (name == "eve_action_guide_status") return statusTool(args);
    if (name == "eve_action_guide_export") return exportTool(args);
    if (name == "eve_action_guide_ingest") return ingestTool(args);
    if (name == "eve_action_guide_verify")
        return error("Unsupported", "Verify (IoU / joint / eve_play) lands in the next slice");
    return error("UnknownTool");
}

std::string_view mcpActionGuideToolSchemas() {
    return R"json({"name":"eve_action_guide_pack_validate","description":"Decode and re-encode an eve.action.guide-pack v1 document. Rejects unknown fields and invalid cameras/shots.","inputSchema":{"type":"object","properties":{"pack":{"description":"Guide-pack object or JSON string"}},"required":["pack"]}},{"name":"eve_action_guide_pack_set","description":"Store a validated guide pack in the process-local MCP session. Optional expectedRevision detects conflicts.","inputSchema":{"type":"object","properties":{"pack":{},"expectedRevision":{"type":"integer","minimum":0}},"required":["pack"]}},{"name":"eve_action_guide_pack_get","description":"Return the process-local session guide pack.","inputSchema":{"type":"object","properties":{}}},{"name":"eve_action_guide_landmarks","description":"Project world-space bones into guide-canvas landmarks (top-left pixels + view depth). Uses pack argument or the session pack.","inputSchema":{"type":"object","properties":{"pack":{},"bones":{"type":"array","items":{"type":"object","properties":{"boneId":{"type":"string"},"name":{"type":"string"},"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},"parentId":{"type":"string"}},"required":["boneId","position"]}}},"required":["bones"]}},{"name":"eve_action_guide_capture","description":"Queue silhouette/depth AOV capture via IActionGuideCapture. Returns ProviderAbsent when no provider is registered.","inputSchema":{"type":"object","properties":{"pack":{},"shotIds":{"type":"array","items":{"type":"string"}}}}},{"name":"eve_action_guide_status","description":"Poll a guide-capture job token without waiting on GPU.","inputSchema":{"type":"object","properties":{"token":{"type":"string"}},"required":["token"]}},{"name":"eve_action_guide_export","description":"Export a Ready guide-capture job to guide-pack/shots/NNNN/{silhouette,depth}.png + manifest.json.","inputSchema":{"type":"object","properties":{"token":{"type":"string"},"path":{"type":"string"}},"required":["token","path"]}},{"name":"eve_action_guide_ingest","description":"Validate AI candidate frames against the guide pack, write sequence PNGs + clip.json + ability.draft.json. Does not register AbilityRuntime.","inputSchema":{"type":"object","properties":{"candidateDir":{"type":"string"},"outputDir":{"type":"string"},"pack":{},"expectedRevision":{"type":"integer","minimum":0},"clipName":{"type":"string"},"commitAbility":{"type":"boolean"}},"required":["candidateDir","outputDir"]}},{"name":"eve_action_guide_verify","description":"Play/verify ingested clips against silhouette/joint thresholds (later slice).","inputSchema":{"type":"object","properties":{"packRevision":{"type":"integer"}}}})json";
}

}  // namespace eve::dev

namespace eve::dev {
EVE_MCP_TOOL_FAMILY(ActionGuide, mcpActionGuideToolSchemas, callMcpActionGuideTool, ContentEnvelope)
}  // namespace eve::dev
