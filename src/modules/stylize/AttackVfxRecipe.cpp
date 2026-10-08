#include "stylize/AttackVfxRecipe.h"

#include "common/Value.h"

#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace eve::stylize {
namespace {

Result<void> rejectUnknown(const Value::Object& object, const std::set<std::string>& allowed,
                           const std::string& path) {
    for (const auto& [key, value] : object) {
        (void)value;
        if (!allowed.contains(key))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("unknown attack VFX field"), std::string(path + "." + key)));
    }
    return Result<void>::success();
}

Result<float> number(const Value& value, const std::string& path) {
    if (const auto* v = value.getIf<double>()) {
        if (!std::isfinite(*v)) return Result<float>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected finite number"), std::string(path)));
        return Result<float>::success(static_cast<float>(*v));
    }
    if (const auto* v = value.getIf<std::int64_t>())
        return Result<float>::success(static_cast<float>(*v));
    return Result<float>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected number"), std::string(path)));
}

Result<double> number64(const Value& value, const std::string& path) {
    if (const auto* v = value.getIf<double>()) {
        if (!std::isfinite(*v)) return Result<double>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected finite number"), std::string(path)));
        return Result<double>::success(*v);
    }
    if (const auto* v = value.getIf<std::int64_t>()) return Result<double>::success(static_cast<double>(*v));
    return Result<double>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected number"), std::string(path)));
}

Result<LogicalId> parseLogicalId(const Value& value, const std::string& path) {
    if (!value.isString()) return Result<LogicalId>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected logical id string"), std::string(path)));
    auto parsed = LogicalId::parse(value.asString());
    if (!parsed) return Result<LogicalId>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("logical id must be namespace:name"), std::string(path)));
    return Result<LogicalId>::success(std::move(*parsed));
}

bool finiteUnit(float value) { return std::isfinite(value) && value >= 0.f && value <= 1.f; }

Result<AttackVfxVec3> parseVec3(const Value& value, const std::string& path) {
    const auto* array = value.getIf<Value::Array>();
    if (!array || array->size() != 3)
        return Result<AttackVfxVec3>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected three-number array"), std::string(path)));
    AttackVfxVec3 out;
    float* components[] = {&out.x, &out.y, &out.z};
    for (std::size_t i = 0; i < 3; ++i) {
        auto parsed = number((*array)[i], path + "[" + std::to_string(i) + "]");
        if (!parsed) return Result<AttackVfxVec3>::failure(parsed.status());
        *components[i] = std::move(parsed).takeValue();
    }
    return Result<AttackVfxVec3>::success(out);
}

Result<AttackVfxPhaseKind> parsePhaseKind(const Value& value, const std::string& path) {
    if (!value.isString()) return Result<AttackVfxPhaseKind>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected string"), std::string(path)));
    const auto& text = value.asString();
    if (text == "anticipate") return Result<AttackVfxPhaseKind>::success(AttackVfxPhaseKind::Anticipate);
    if (text == "release") return Result<AttackVfxPhaseKind>::success(AttackVfxPhaseKind::Release);
    if (text == "travel") return Result<AttackVfxPhaseKind>::success(AttackVfxPhaseKind::Travel);
    if (text == "impact") return Result<AttackVfxPhaseKind>::success(AttackVfxPhaseKind::Impact);
    if (text == "aftermath") return Result<AttackVfxPhaseKind>::success(AttackVfxPhaseKind::Aftermath);
    if (text == "status") return Result<AttackVfxPhaseKind>::success(AttackVfxPhaseKind::Status);
    return Result<AttackVfxPhaseKind>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("unknown phase kind"), std::string(path)));
}

Result<AttackVfxLayerRole> parseLayerRole(const Value& value, const std::string& path) {
    if (!value.isString()) return Result<AttackVfxLayerRole>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected string"), std::string(path)));
    const auto& text = value.asString();
    if (text == "blockingMesh") return Result<AttackVfxLayerRole>::success(AttackVfxLayerRole::BlockingMesh);
    if (text == "meshVfx") return Result<AttackVfxLayerRole>::success(AttackVfxLayerRole::MeshVfx);
    if (text == "trail") return Result<AttackVfxLayerRole>::success(AttackVfxLayerRole::Trail);
    if (text == "particles") return Result<AttackVfxLayerRole>::success(AttackVfxLayerRole::Particles);
    if (text == "decal") return Result<AttackVfxLayerRole>::success(AttackVfxLayerRole::Decal);
    if (text == "distortion") return Result<AttackVfxLayerRole>::success(AttackVfxLayerRole::Distortion);
    if (text == "camera") return Result<AttackVfxLayerRole>::success(AttackVfxLayerRole::Camera);
    if (text == "prefab") return Result<AttackVfxLayerRole>::success(AttackVfxLayerRole::Prefab);
    if (text == "audio") return Result<AttackVfxLayerRole>::success(AttackVfxLayerRole::Audio);
    return Result<AttackVfxLayerRole>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("unknown layer role"), std::string(path)));
}

Result<AttackVfxStopBehavior> parseStopBehavior(const Value& value, const std::string& path) {
    if (!value.isString())
        return Result<AttackVfxStopBehavior>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected string"), std::string(path)));
    const auto& text = value.asString();
    if (text == "stopEmitting") return Result<AttackVfxStopBehavior>::success(AttackVfxStopBehavior::StopEmitting);
    if (text == "clearImmediately")
        return Result<AttackVfxStopBehavior>::success(AttackVfxStopBehavior::ClearImmediately);
    return Result<AttackVfxStopBehavior>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("unknown stopBehavior"), std::string(path)));
}

Result<AttackVfxSpatialAttachment> parseAttachment(const Value& value, const std::string& path) {
    if (!value.isString())
        return Result<AttackVfxSpatialAttachment>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected string"), std::string(path)));
    const auto& text = value.asString();
    if (text == "followTarget")
        return Result<AttackVfxSpatialAttachment>::success(AttackVfxSpatialAttachment::FollowTarget);
    if (text == "followPositionOnly")
        return Result<AttackVfxSpatialAttachment>::success(AttackVfxSpatialAttachment::FollowPositionOnly);
    if (text == "worldTransformAtStart")
        return Result<AttackVfxSpatialAttachment>::success(AttackVfxSpatialAttachment::WorldTransformAtStart);
    return Result<AttackVfxSpatialAttachment>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("unknown attachment"), std::string(path)));
}

Result<AttackVfxSpatialAnchor> parseAnchor(const Value& value, const std::string& path) {
    if (!value.isString())
        return Result<AttackVfxSpatialAnchor>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected string"), std::string(path)));
    const auto& text = value.asString();
    if (text == "source") return Result<AttackVfxSpatialAnchor>::success(AttackVfxSpatialAnchor::Source);
    if (text == "target") return Result<AttackVfxSpatialAnchor>::success(AttackVfxSpatialAnchor::Target);
    return Result<AttackVfxSpatialAnchor>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("unknown anchor"), std::string(path)));
}

Result<AttackVfxSpatial> parseSpatial(const Value* value, const std::string& path) {
    AttackVfxSpatial spatial;
    if (!value) return Result<AttackVfxSpatial>::success(spatial);
    const auto* object = value->getIf<Value::Object>();
    if (!object) return Result<AttackVfxSpatial>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected object"), std::string(path)));
    auto known = rejectUnknown(*object,
                               {"attachment", "anchor", "targetIndex", "bone", "positionOffset",
                                "rotationOffsetDegrees", "scale"},
                               path);
    if (!known) return Result<AttackVfxSpatial>::failure(known.status());

    if (const auto it = object->find("attachment"); it != object->end()) {
        auto parsed = parseAttachment(it->second, path + ".attachment");
        if (!parsed) return Result<AttackVfxSpatial>::failure(parsed.status());
        spatial.attachment = std::move(parsed).takeValue();
    }
    if (const auto it = object->find("anchor"); it != object->end()) {
        auto parsed = parseAnchor(it->second, path + ".anchor");
        if (!parsed) return Result<AttackVfxSpatial>::failure(parsed.status());
        spatial.anchor = std::move(parsed).takeValue();
    }
    if (const auto it = object->find("targetIndex"); it != object->end()) {
        const auto* index = it->second.getIf<std::int64_t>();
        if (!index || *index < 0)
            return Result<AttackVfxSpatial>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("targetIndex must be >= 0"), std::string(path + ".targetIndex")));
        spatial.targetIndex = static_cast<std::size_t>(*index);
    }
    if (const auto it = object->find("bone"); it != object->end()) {
        if (!it->second.isString())
            return Result<AttackVfxSpatial>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("bone must be a string"), std::string(path + ".bone")));
        spatial.bone = it->second.asString();
    }
    if (const auto it = object->find("positionOffset"); it != object->end()) {
        auto parsed = parseVec3(it->second, path + ".positionOffset");
        if (!parsed) return Result<AttackVfxSpatial>::failure(parsed.status());
        spatial.positionOffset = std::move(parsed).takeValue();
    }
    if (const auto it = object->find("rotationOffsetDegrees"); it != object->end()) {
        auto parsed = parseVec3(it->second, path + ".rotationOffsetDegrees");
        if (!parsed) return Result<AttackVfxSpatial>::failure(parsed.status());
        spatial.rotationOffsetDegrees = std::move(parsed).takeValue();
    }
    if (const auto it = object->find("scale"); it != object->end()) {
        auto parsed = parseVec3(it->second, path + ".scale");
        if (!parsed) return Result<AttackVfxSpatial>::failure(parsed.status());
        spatial.scale = std::move(parsed).takeValue();
    }
    auto valid = spatial.validate(path);
    if (!valid) return Result<AttackVfxSpatial>::failure(valid.status());
    return Result<AttackVfxSpatial>::success(spatial);
}

Result<std::map<std::string, float>> parseFloatMap(const Value* value, const std::string& path) {
    std::map<std::string, float> out;
    if (!value) return Result<std::map<std::string, float>>::success(out);
    const auto* object = value->getIf<Value::Object>();
    if (!object)
        return Result<std::map<std::string, float>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected object"), std::string(path)));
    for (const auto& [name, raw] : *object) {
        auto parsed = number(raw, path + "." + name);
        if (!parsed) return Result<std::map<std::string, float>>::failure(parsed.status());
        out.emplace(name, std::move(parsed).takeValue());
    }
    return Result<std::map<std::string, float>>::success(std::move(out));
}

Result<std::map<std::string, std::string>> parseStringMap(const Value* value, const std::string& path) {
    std::map<std::string, std::string> out;
    if (!value) return Result<std::map<std::string, std::string>>::success(out);
    const auto* object = value->getIf<Value::Object>();
    if (!object)
        return Result<std::map<std::string, std::string>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected object"), std::string(path)));
    for (const auto& [name, raw] : *object) {
        if (!raw.isString())
            return Result<std::map<std::string, std::string>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected string"), std::string(path + "." + name)));
        out.emplace(name, raw.asString());
    }
    return Result<std::map<std::string, std::string>>::success(std::move(out));
}

Result<AttackVfxLayer> parseLayer(const Value& value, const std::string& path) {
    const auto* object = value.getIf<Value::Object>();
    if (!object) return Result<AttackVfxLayer>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected object"), std::string(path)));
    auto known =
        rejectUnknown(*object, {"role", "uri", "spatial", "floatParams", "stopBehavior"}, path);
    if (!known) return Result<AttackVfxLayer>::failure(known.status());

    AttackVfxLayer layer;
    const auto role = object->find("role");
    if (role == object->end())
        return Result<AttackVfxLayer>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("role is required"), std::string(path + ".role")));
    auto parsedRole = parseLayerRole(role->second, path + ".role");
    if (!parsedRole)
        return Result<AttackVfxLayer>::failure(parsedRole.status());
    layer.role = std::move(parsedRole).takeValue();

    if (const auto it = object->find("uri"); it != object->end()) {
        if (!it->second.isString())
            return Result<AttackVfxLayer>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("uri must be a string"), std::string(path + ".uri")));
        layer.uri = it->second.asString();
    }
    if (const auto it = object->find("spatial"); it != object->end()) {
        auto parsed = parseSpatial(&it->second, path + ".spatial");
        if (!parsed)
            return Result<AttackVfxLayer>::failure(parsed.status());
        layer.spatial = std::move(parsed).takeValue();
    }
    if (const auto it = object->find("floatParams"); it != object->end()) {
        auto parsed = parseFloatMap(&it->second, path + ".floatParams");
        if (!parsed)
            return Result<AttackVfxLayer>::failure(parsed.status());
        layer.floatParams = std::move(parsed).takeValue();
    }
    if (const auto it = object->find("stopBehavior"); it != object->end()) {
        auto parsed = parseStopBehavior(it->second, path + ".stopBehavior");
        if (!parsed)
            return Result<AttackVfxLayer>::failure(parsed.status());
        layer.stopBehavior = std::move(parsed).takeValue();
    }
    auto valid = layer.validate(path);
    if (!valid) return Result<AttackVfxLayer>::failure(valid.status());
    return Result<AttackVfxLayer>::success(std::move(layer));
}

Result<AttackVfxPhase> parsePhase(const Value& value, const std::string& path) {
    const auto* object = value.getIf<Value::Object>();
    if (!object) return Result<AttackVfxPhase>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected object"), std::string(path)));
    auto known = rejectUnknown(
        *object, {"kind", "startCue", "endCue", "startOffsetSeconds", "durationSeconds", "layers"}, path);
    if (!known) return Result<AttackVfxPhase>::failure(known.status());

    AttackVfxPhase phase;
    const auto kind = object->find("kind");
    if (kind == object->end())
        return Result<AttackVfxPhase>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("kind is required"), std::string(path + ".kind")));
    auto parsedKind = parsePhaseKind(kind->second, path + ".kind");
    if (!parsedKind)
        return Result<AttackVfxPhase>::failure(parsedKind.status());
    phase.kind = std::move(parsedKind).takeValue();

    if (const auto it = object->find("startCue"); it != object->end()) {
        if (!it->second.isString())
            return Result<AttackVfxPhase>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("startCue must be a string"), std::string(path + ".startCue")));
        phase.startCue = it->second.asString();
    }
    if (const auto it = object->find("endCue"); it != object->end()) {
        if (!it->second.isString())
            return Result<AttackVfxPhase>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("endCue must be a string"), std::string(path + ".endCue")));
        phase.endCue = it->second.asString();
    }
    if (const auto it = object->find("startOffsetSeconds"); it != object->end()) {
        auto parsed = number64(it->second, path + ".startOffsetSeconds");
        if (!parsed)
            return Result<AttackVfxPhase>::failure(parsed.status());
        phase.startOffsetSeconds = std::move(parsed).takeValue();
    }
    if (const auto it = object->find("durationSeconds"); it != object->end()) {
        auto parsed = number64(it->second, path + ".durationSeconds");
        if (!parsed)
            return Result<AttackVfxPhase>::failure(parsed.status());
        phase.durationSeconds = std::move(parsed).takeValue();
    }
    if (const auto it = object->find("layers"); it != object->end()) {
        const auto* layers = it->second.getIf<Value::Array>();
        if (!layers)
            return Result<AttackVfxPhase>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("layers must be an array"), std::string(path + ".layers")));
        phase.layers.reserve(layers->size());
        for (std::size_t i = 0; i < layers->size(); ++i) {
            auto parsed = parseLayer((*layers)[i], path + ".layers[" + std::to_string(i) + "]");
            if (!parsed)
                return Result<AttackVfxPhase>::failure(parsed.status());
            phase.layers.push_back(std::move(parsed).takeValue());
        }
    }
    auto valid = phase.validate(path);
    if (!valid) return Result<AttackVfxPhase>::failure(valid.status());
    return Result<AttackVfxPhase>::success(std::move(phase));
}

Result<AttackVfxPalette> parsePalette(const Value* value, const std::string& path) {
    AttackVfxPalette palette;
    if (!value) return Result<AttackVfxPalette>::success(palette);
    const auto* object = value->getIf<Value::Object>();
    if (!object) return Result<AttackVfxPalette>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected object"), std::string(path)));
    auto known = rejectUnknown(*object, {"primary", "secondary", "emissive"}, path);
    if (!known) return Result<AttackVfxPalette>::failure(known.status());
    if (const auto it = object->find("primary"); it != object->end()) {
        auto parsed = parseVec3(it->second, path + ".primary");
        if (!parsed)
            return Result<AttackVfxPalette>::failure(parsed.status());
        palette.primary = std::move(parsed).takeValue();
    }
    if (const auto it = object->find("secondary"); it != object->end()) {
        auto parsed = parseVec3(it->second, path + ".secondary");
        if (!parsed)
            return Result<AttackVfxPalette>::failure(parsed.status());
        palette.secondary = std::move(parsed).takeValue();
    }
    if (const auto it = object->find("emissive"); it != object->end()) {
        auto parsed = parseVec3(it->second, path + ".emissive");
        if (!parsed)
            return Result<AttackVfxPalette>::failure(parsed.status());
        palette.emissive = std::move(parsed).takeValue();
    }
    return Result<AttackVfxPalette>::success(palette);
}

Result<AttackVfxSkin> parseSkinObject(const Value::Object& object, const std::string& path, bool requireSchema) {
    auto known = rejectUnknown(object,
                               {"schema", "schemaVersion", "id", "palette", "styleHints", "shakeProfile",
                                "distortionProfile", "statusOverlayUri"},
                               path);
    if (!known) return Result<AttackVfxSkin>::failure(known.status());

    if (requireSchema) {
        const auto schema = object.find("schema");
        if (schema == object.end() || !schema->second.isString() ||
            schema->second.asString() != "eve.stylize.attack-vfx-skin")
            return Result<AttackVfxSkin>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("schema must be eve.stylize.attack-vfx-skin"), std::string(path + ".schema")));
        const auto version = object.find("schemaVersion");
        if (version == object.end())
            return Result<AttackVfxSkin>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("schemaVersion is required"), std::string(path + ".schemaVersion")));
        const auto* ver = version->second.getIf<std::int64_t>();
        if (!ver || *ver != 1)
            return Result<AttackVfxSkin>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("unsupported skin schemaVersion"), std::string(path + ".schemaVersion")));
    }

    AttackVfxSkin skin;
    const auto id = object.find("id");
    if (id == object.end())
        return Result<AttackVfxSkin>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("id is required"), std::string(path + ".id")));
    auto parsedId = parseLogicalId(id->second, path + ".id");
    if (!parsedId)
        return Result<AttackVfxSkin>::failure(parsedId.status());
    skin.id = std::move(parsedId).takeValue();

    if (const auto it = object.find("palette"); it != object.end()) {
        auto parsed = parsePalette(&it->second, path + ".palette");
        if (!parsed)
            return Result<AttackVfxSkin>::failure(parsed.status());
        skin.palette = std::move(parsed).takeValue();
    }
    if (const auto it = object.find("styleHints"); it != object.end()) {
        auto parsed = parseStringMap(&it->second, path + ".styleHints");
        if (!parsed)
            return Result<AttackVfxSkin>::failure(parsed.status());
        skin.styleHints = std::move(parsed).takeValue();
    }
    if (const auto it = object.find("shakeProfile"); it != object.end()) {
        auto parsed = parseFloatMap(&it->second, path + ".shakeProfile");
        if (!parsed)
            return Result<AttackVfxSkin>::failure(parsed.status());
        skin.shakeProfile = std::move(parsed).takeValue();
    }
    if (const auto it = object.find("distortionProfile"); it != object.end()) {
        auto parsed = parseFloatMap(&it->second, path + ".distortionProfile");
        if (!parsed)
            return Result<AttackVfxSkin>::failure(parsed.status());
        skin.distortionProfile = std::move(parsed).takeValue();
    }
    if (const auto it = object.find("statusOverlayUri"); it != object.end()) {
        if (!it->second.isString())
            return Result<AttackVfxSkin>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("statusOverlayUri must be a string"), std::string(path + ".statusOverlayUri")));
        skin.statusOverlayUri = it->second.asString();
    }
    auto valid = skin.validate();
    if (!valid) return Result<AttackVfxSkin>::failure(valid.status());
    return Result<AttackVfxSkin>::success(std::move(skin));
}

Result<AttackVfxBudget> parseBudget(const Value* value, const std::string& path) {
    AttackVfxBudget budget;
    if (!value) return Result<AttackVfxBudget>::success(budget);
    const auto* object = value->getIf<Value::Object>();
    if (!object) return Result<AttackVfxBudget>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("expected object"), std::string(path)));
    auto known = rejectUnknown(*object, {"lod", "maxParticles", "allowDistortion"}, path);
    if (!known) return Result<AttackVfxBudget>::failure(known.status());
    if (const auto it = object->find("lod"); it != object->end()) {
        const auto* lod = it->second.getIf<std::int64_t>();
        if (!lod || *lod < 0)
            return Result<AttackVfxBudget>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("lod must be >= 0"), std::string(path + ".lod")));
        if (*lod > static_cast<std::int64_t>(std::numeric_limits<int>::max()))
            return Result<AttackVfxBudget>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("lod exceeds int range"), std::string(path + ".lod")));
        budget.lod = static_cast<int>(*lod);
    }
    if (const auto it = object->find("maxParticles"); it != object->end()) {
        const auto* count = it->second.getIf<std::int64_t>();
        if (!count || *count < 0)
            return Result<AttackVfxBudget>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("maxParticles must be >= 0"), std::string(path + ".maxParticles")));
        if (*count > static_cast<std::int64_t>(std::numeric_limits<int>::max()))
            return Result<AttackVfxBudget>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("maxParticles exceeds int range"), std::string(path + ".maxParticles")));
        budget.maxParticles = static_cast<int>(*count);
    }
    if (const auto it = object->find("allowDistortion"); it != object->end()) {
        const auto* enabled = it->second.getIf<bool>();
        if (!enabled)
            return Result<AttackVfxBudget>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("allowDistortion must be boolean"), std::string(path + ".allowDistortion")));
        budget.allowDistortion = *enabled;
    }
    return Result<AttackVfxBudget>::success(budget);
}

}  // namespace

std::string_view attackVfxPhaseKindName(AttackVfxPhaseKind kind) noexcept {
    switch (kind) {
        case AttackVfxPhaseKind::Anticipate: return "anticipate";
        case AttackVfxPhaseKind::Release: return "release";
        case AttackVfxPhaseKind::Travel: return "travel";
        case AttackVfxPhaseKind::Impact: return "impact";
        case AttackVfxPhaseKind::Aftermath: return "aftermath";
        case AttackVfxPhaseKind::Status: return "status";
    }
    return "release";
}

std::string_view attackVfxLayerRoleName(AttackVfxLayerRole role) noexcept {
    switch (role) {
        case AttackVfxLayerRole::BlockingMesh: return "blockingMesh";
        case AttackVfxLayerRole::MeshVfx: return "meshVfx";
        case AttackVfxLayerRole::Trail: return "trail";
        case AttackVfxLayerRole::Particles: return "particles";
        case AttackVfxLayerRole::Decal: return "decal";
        case AttackVfxLayerRole::Distortion: return "distortion";
        case AttackVfxLayerRole::Camera: return "camera";
        case AttackVfxLayerRole::Prefab: return "prefab";
        case AttackVfxLayerRole::Audio: return "audio";
    }
    return "particles";
}

std::string_view attackVfxStopBehaviorName(AttackVfxStopBehavior behavior) noexcept {
    switch (behavior) {
        case AttackVfxStopBehavior::StopEmitting: return "stopEmitting";
        case AttackVfxStopBehavior::ClearImmediately: return "clearImmediately";
    }
    return "stopEmitting";
}

std::string_view attackVfxSpatialAttachmentName(AttackVfxSpatialAttachment mode) noexcept {
    switch (mode) {
        case AttackVfxSpatialAttachment::FollowTarget: return "followTarget";
        case AttackVfxSpatialAttachment::FollowPositionOnly: return "followPositionOnly";
        case AttackVfxSpatialAttachment::WorldTransformAtStart: return "worldTransformAtStart";
    }
    return "followTarget";
}

std::string_view attackVfxSpatialAnchorName(AttackVfxSpatialAnchor anchor) noexcept {
    switch (anchor) {
        case AttackVfxSpatialAnchor::Source: return "source";
        case AttackVfxSpatialAnchor::Target: return "target";
    }
    return "source";
}

Result<void> AttackVfxSpatial::validate(std::string_view path) const {
    const auto finite = [](float v) { return std::isfinite(v); };
    if (!finite(positionOffset.x) || !finite(positionOffset.y) || !finite(positionOffset.z))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("positionOffset must be finite"), std::string(std::string(path) + ".positionOffset")));
    if (!finite(rotationOffsetDegrees.x) || !finite(rotationOffsetDegrees.y) || !finite(rotationOffsetDegrees.z))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("rotationOffsetDegrees must be finite"), std::string(std::string(path) + ".rotationOffsetDegrees")));
    if (!(std::isfinite(scale.x) && scale.x > 0.f) || !(std::isfinite(scale.y) && scale.y > 0.f) ||
        !(std::isfinite(scale.z) && scale.z > 0.f))
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("scale components must be finite and > 0"), std::string(std::string(path) + ".scale")));
    return Result<void>::success();
}

Result<void> AttackVfxLayer::validate(std::string_view path) const {
    auto spatialStatus = spatial.validate(std::string(path) + ".spatial");
    if (!spatialStatus) return spatialStatus;
    const bool uriOptional =
        role == AttackVfxLayerRole::Camera || role == AttackVfxLayerRole::Distortion;
    if (!uriOptional && uri.empty())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("uri is required for this layer role"), std::string(std::string(path) + ".uri")));
    for (const auto& [name, value] : floatParams) {
        (void)name;
        if (!std::isfinite(value))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("floatParams values must be finite"), std::string(std::string(path) + ".floatParams")));
    }
    return Result<void>::success();
}

Result<void> AttackVfxPhase::validate(std::string_view path) const {
    if (!std::isfinite(startOffsetSeconds) || startOffsetSeconds < 0.0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("startOffsetSeconds must be finite and >= 0"), std::string(std::string(path) + ".startOffsetSeconds")));
    if (!std::isfinite(durationSeconds) || durationSeconds < 0.0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("durationSeconds must be finite and >= 0"), std::string(std::string(path) + ".durationSeconds")));
    for (std::size_t i = 0; i < layers.size(); ++i) {
        auto status = layers[i].validate(std::string(path) + ".layers[" + std::to_string(i) + "]");
        if (!status) return status;
    }
    return Result<void>::success();
}

Result<void> AttackVfxSkin::validate() const {
    if (!id.isValid()) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("skin id is required"), std::string("id")));
    const auto checkColor = [](const AttackVfxVec3& c, std::string_view path) -> Result<void> {
        if (!finiteUnit(c.x) || !finiteUnit(c.y) || !finiteUnit(c.z))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("palette channel must be in [0,1]"), std::string(path)));
        return Result<void>::success();
    };
    if (auto s = checkColor(palette.primary, "palette.primary"); !s) return s;
    if (auto s = checkColor(palette.secondary, "palette.secondary"); !s) return s;
    if (auto s = checkColor(palette.emissive, "palette.emissive"); !s) return s;
    for (const auto& [name, value] : shakeProfile) {
        (void)name;
        if (!std::isfinite(value))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("shakeProfile values must be finite"), std::string("shakeProfile")));
    }
    for (const auto& [name, value] : distortionProfile) {
        (void)name;
        if (!std::isfinite(value))
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("distortionProfile values must be finite"), std::string("distortionProfile")));
    }
    return Result<void>::success();
}

Result<void> AttackVfxRecipe::validate() const {
    if (!id.isValid()) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("recipe id is required"), std::string("id")));
    if (phases.empty()) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("recipe requires at least one phase"), std::string("phases")));
    if (budget.lod < 0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("lod must be >= 0"), std::string("budget.lod")));
    if (budget.maxParticles < 0)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("maxParticles must be >= 0"), std::string("budget.maxParticles")));
    for (std::size_t i = 0; i < phases.size(); ++i) {
        auto status = phases[i].validate("phases[" + std::to_string(i) + "]");
        if (!status) return status;
    }
    if (skin) {
        auto status = skin->validate();
        if (!status) return status;
        if (skinId && skinId->format() != skin->id.format())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("inline skin.id must match skinId"), std::string("skin.id")));
    }
    return Result<void>::success();
}

Result<AttackVfxSkin> AttackVfxSkin::fromJson(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed) return Result<AttackVfxSkin>::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (!object) return Result<AttackVfxSkin>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("skin must be an object"), std::string("")));
    return parseSkinObject(*object, "", true);
}

Result<AttackVfxRecipe> AttackVfxRecipe::fromJson(std::string_view json) {
    auto parsed = Value::fromJson(json);
    if (!parsed)
        return Result<AttackVfxRecipe>::failure(parsed.status());
    const auto* object = parsed.value().getIf<Value::Object>();
    if (!object) return Result<AttackVfxRecipe>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("recipe must be an object"), std::string("")));

    auto known = rejectUnknown(*object, {"schema", "schemaVersion", "id", "phases", "skinId", "skin", "budget"}, "");
    if (!known)
        return Result<AttackVfxRecipe>::failure(known.status());

    const auto schema = object->find("schema");
    if (schema == object->end() || !schema->second.isString() || schema->second.asString() != schemaId)
        return Result<AttackVfxRecipe>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("schema must be eve.stylize.attack-vfx"), std::string("schema")));
    const auto version = object->find("schemaVersion");
    if (version == object->end())
        return Result<AttackVfxRecipe>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("schemaVersion is required"), std::string("schemaVersion")));
    const auto* ver = version->second.getIf<std::int64_t>();
    if (!ver || *ver != static_cast<std::int64_t>(schemaVersion))
        return Result<AttackVfxRecipe>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("unsupported schemaVersion"), std::string("schemaVersion")));

    AttackVfxRecipe recipe;
    const auto id = object->find("id");
    if (id == object->end())
        return Result<AttackVfxRecipe>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("id is required"), std::string("id")));
    auto parsedId = parseLogicalId(id->second, "id");
    if (!parsedId)
        return Result<AttackVfxRecipe>::failure(parsedId.status());
    recipe.id = std::move(parsedId).takeValue();

    const auto phases = object->find("phases");
    if (phases == object->end())
        return Result<AttackVfxRecipe>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("phases is required"), std::string("phases")));
    const auto* phaseArray = phases->second.getIf<Value::Array>();
    if (!phaseArray)
        return Result<AttackVfxRecipe>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("phases must be an array"), std::string("phases")));
    recipe.phases.reserve(phaseArray->size());
    for (std::size_t i = 0; i < phaseArray->size(); ++i) {
        auto parsedPhase = parsePhase((*phaseArray)[i], "phases[" + std::to_string(i) + "]");
        if (!parsedPhase)
            return Result<AttackVfxRecipe>::failure(parsedPhase.status());
        recipe.phases.push_back(std::move(parsedPhase).takeValue());
    }

    if (const auto it = object->find("skinId"); it != object->end()) {
        auto parsedSkinId = parseLogicalId(it->second, "skinId");
        if (!parsedSkinId)
            return Result<AttackVfxRecipe>::failure(parsedSkinId.status());
        recipe.skinId = std::move(parsedSkinId).takeValue();
    }
    if (const auto it = object->find("skin"); it != object->end()) {
        const auto* skinObject = it->second.getIf<Value::Object>();
        if (!skinObject)
            return Result<AttackVfxRecipe>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, std::string("skin must be an object"), std::string("skin")));
        // Inline skin omits schema fields; id is still required.
        auto parsedSkin = parseSkinObject(*skinObject, "skin", false);
        if (!parsedSkin)
            return Result<AttackVfxRecipe>::failure(parsedSkin.status());
        recipe.skin = std::move(parsedSkin).takeValue();
        if (!recipe.skinId) recipe.skinId = recipe.skin->id;
    }
    if (const auto it = object->find("budget"); it != object->end()) {
        auto parsedBudget = parseBudget(&it->second, "budget");
        if (!parsedBudget)
            return Result<AttackVfxRecipe>::failure(parsedBudget.status());
        recipe.budget = std::move(parsedBudget).takeValue();
    }

    auto valid = recipe.validate();
    if (!valid)
        return Result<AttackVfxRecipe>::failure(valid.status());
    return Result<AttackVfxRecipe>::success(std::move(recipe));
}

namespace {

Value makeVec3(const AttackVfxVec3& value) {
    Value::Array array;
    array.emplace_back(static_cast<double>(value.x));
    array.emplace_back(static_cast<double>(value.y));
    array.emplace_back(static_cast<double>(value.z));
    return Value(std::move(array));
}

Value::Object encodeSpatial(const AttackVfxSpatial& spatial) {
    Value::Object object;
    object.emplace("attachment", Value(std::string(attackVfxSpatialAttachmentName(spatial.attachment))));
    object.emplace("anchor", Value(std::string(attackVfxSpatialAnchorName(spatial.anchor))));
    object.emplace("targetIndex", Value(static_cast<std::int64_t>(spatial.targetIndex)));
    if (!spatial.bone.empty()) object.emplace("bone", Value(spatial.bone));
    object.emplace("positionOffset", makeVec3(spatial.positionOffset));
    object.emplace("rotationOffsetDegrees", makeVec3(spatial.rotationOffsetDegrees));
    object.emplace("scale", makeVec3(spatial.scale));
    return object;
}

Value::Object encodeFloatMap(const std::map<std::string, float>& values) {
    Value::Object object;
    for (const auto& [name, value] : values) object.emplace(name, Value(static_cast<double>(value)));
    return object;
}

Value::Object encodeStringMap(const std::map<std::string, std::string>& values) {
    Value::Object object;
    for (const auto& [name, value] : values) object.emplace(name, Value(value));
    return object;
}

Value::Object encodeLayer(const AttackVfxLayer& layer) {
    Value::Object object;
    object.emplace("role", Value(std::string(attackVfxLayerRoleName(layer.role))));
    if (!layer.uri.empty()) object.emplace("uri", Value(layer.uri));
    object.emplace("spatial", Value(encodeSpatial(layer.spatial)));
    if (!layer.floatParams.empty()) object.emplace("floatParams", Value(encodeFloatMap(layer.floatParams)));
    object.emplace("stopBehavior", Value(std::string(attackVfxStopBehaviorName(layer.stopBehavior))));
    return object;
}

Value::Object encodePhase(const AttackVfxPhase& phase) {
    Value::Object object;
    object.emplace("kind", Value(std::string(attackVfxPhaseKindName(phase.kind))));
    if (!phase.startCue.empty()) object.emplace("startCue", Value(phase.startCue));
    if (!phase.endCue.empty()) object.emplace("endCue", Value(phase.endCue));
    object.emplace("startOffsetSeconds", Value(phase.startOffsetSeconds));
    object.emplace("durationSeconds", Value(phase.durationSeconds));
    Value::Array layers;
    layers.reserve(phase.layers.size());
    for (const auto& layer : phase.layers) layers.emplace_back(Value(encodeLayer(layer)));
    object.emplace("layers", Value(std::move(layers)));
    return object;
}

Value::Object encodeSkinBody(const AttackVfxSkin& skin, bool includeSchema) {
    Value::Object object;
    if (includeSchema) {
        object.emplace("schema", Value(std::string("eve.stylize.attack-vfx-skin")));
        object.emplace("schemaVersion", Value(static_cast<std::int64_t>(1)));
    }
    object.emplace("id", Value(skin.id.format()));
    Value::Object palette;
    palette.emplace("primary", makeVec3(skin.palette.primary));
    palette.emplace("secondary", makeVec3(skin.palette.secondary));
    palette.emplace("emissive", makeVec3(skin.palette.emissive));
    object.emplace("palette", Value(std::move(palette)));
    if (!skin.styleHints.empty()) object.emplace("styleHints", Value(encodeStringMap(skin.styleHints)));
    if (!skin.shakeProfile.empty()) object.emplace("shakeProfile", Value(encodeFloatMap(skin.shakeProfile)));
    if (!skin.distortionProfile.empty())
        object.emplace("distortionProfile", Value(encodeFloatMap(skin.distortionProfile)));
    if (!skin.statusOverlayUri.empty()) object.emplace("statusOverlayUri", Value(skin.statusOverlayUri));
    return object;
}

}  // namespace

Result<std::string> AttackVfxSkin::toJson() const {
    auto valid = validate();
    if (!valid) return Result<std::string>::failure(valid.status());
    return Value(encodeSkinBody(*this, true)).toJson();
}

Result<std::string> AttackVfxRecipe::toJson() const {
    auto valid = validate();
    if (!valid) return Result<std::string>::failure(valid.status());
    Value::Object object;
    object.emplace("schema", std::string(schemaId));
    object.emplace("schemaVersion", static_cast<std::int64_t>(schemaVersion));
    object.emplace("id", id.format());
    Value::Array phasesJson;
    phasesJson.reserve(phases.size());
    for (const auto& phase : phases) phasesJson.emplace_back(Value(encodePhase(phase)));
    object.emplace("phases", std::move(phasesJson));
    if (skinId) object.emplace("skinId", skinId->format());
    if (skin) object.emplace("skin", encodeSkinBody(*skin, false));
    Value::Object budgetJson;
    budgetJson.emplace("lod", static_cast<std::int64_t>(budget.lod));
    budgetJson.emplace("maxParticles", static_cast<std::int64_t>(budget.maxParticles));
    budgetJson.emplace("allowDistortion", budget.allowDistortion);
    object.emplace("budget", std::move(budgetJson));
    return Value(std::move(object)).toJson();
}

AttackVfxRecipeSlot::AttackVfxRecipeSlot(AttackVfxRecipe recipe) : recipe_(std::move(recipe)) {}

Result<std::uint64_t> AttackVfxRecipeSlot::reload(std::string_view json) {
    auto parsed = AttackVfxRecipe::fromJson(json);
    if (!parsed)
        return Result<std::uint64_t>::failure(parsed.status());
    recipe_ = std::move(parsed).takeValue();
    ++revision_;
    return Result<std::uint64_t>::success(revision_);
}

}  // namespace eve::stylize
