#include "asset/AssetMigration.h"

#include "data/HashFunction.h"

#include <algorithm>
#include <map>
#include <set>

namespace eve::asset {
namespace {

std::string sha256(std::span<const std::uint8_t> bytes) {
    data::HashFunction::Value digest{};
    data::HashFunction::getHashFunction("sha256")->hash("sha256", reinterpret_cast<const char*>(bytes.data()),
                                                        bytes.size(), digest);
    static constexpr char hex[]  = "0123456789abcdef";
    std::string           result = "sha256:";
    result.reserve(71);
    for (std::size_t index = 0; index < 32; ++index) {
        const auto byte = static_cast<std::uint8_t>(digest.data[index]);
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}

Result<Value> migrateImageV2ToV3(const Value& input) {
    const auto* object = input.getIf<Value::Object>();
    if (!object)
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "eve.image definition must be an object", {}, {}, "asset.migration"));
    const auto  schema      = object->find("schema");
    const auto  version     = object->find("schemaVersion");
    const auto  color       = object->find("color");
    const auto* colorObject = color == object->end() ? nullptr : color->second.getIf<Value::Object>();
    if (schema == object->end() || !schema->second.isString() || schema->second.asString() != "eve.image" ||
        version == object->end() || !version->second.isInt64() || version->second.asInt() != 2 ||
        object->contains("mipCount") || !colorObject || !colorObject->contains("transfer") ||
        !colorObject->at("transfer").isString() ||
        (colorObject->at("transfer").asString() != "srgb" && colorObject->at("transfer").asString() != "linear"))
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "eve.image/2 definition is malformed or contains unversioned mips", {}, {},
            "asset.migration"));
    Value::Object migrated    = *object;
    migrated["schemaVersion"] = Value(std::int64_t(3));
    migrated["mipCount"]      = Value(std::int64_t(1));
    return Result<Value>::success(Value(std::move(migrated)));
}

Result<Value> migrateDefinition(std::string_view type, SchemaVersion from, SchemaVersion current,
                                const Value& definition) {
    if (from == current) return Result<Value>::success(definition);
    if (from.value() > current.value())
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::UnknownVersion,
                                                        "asset definition is newer than this reader", std::string(type),
                                                        {}, "asset.migration"));
    if (type == "eve.mesh" && current.value() == 3 && (from.value() == 1 || from.value() == 2)) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.mesh" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() ||
            object->at("schemaVersion").asInt() != static_cast<std::int64_t>(from.value()))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.mesh definition is malformed", {}, {}, "asset.migration"));
        auto migrated = *object;
        if (from.value() == 1) {
            if (migrated.contains("texcoordSets") || migrated.contains("colors"))
                return Result<Value>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "eve.mesh/1 contains reserved metadata", {}, {}, "asset.migration"));
            if (auto uv = migrated.find("texcoord0"); uv != migrated.end()) {
                if (!uv->second.isBool())
                    return Result<Value>::failure(Diagnostic::error(
                        DiagnosticCode::ParseError, "invalid legacy UV metadata", {}, {}, "asset.migration"));
                Value::Array sets;
                if (uv->second.asBool()) sets.emplace_back(int64_t(0));
                migrated.erase(uv);
                migrated["texcoordSets"] = Value(std::move(sets));
            }
        } else if (migrated.contains("colors")) {
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.mesh/2 contains reserved color metadata", {}, {}, "asset.migration"));
        }
        migrated["colors"]       = Value(false);
        migrated["schemaVersion"] = Value(int64_t(3));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (from.value() + 1 != current.value() && type != "eve.instance-set")
        return Result<Value>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                        "asset definition is older than the N-1 compatibility window",
                                                        std::string(type), {}, "asset.migration"));
    if (type == "eve.instance-set" && from.value() == 1 && current.value() >= 2) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.instance-set" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 1 ||
            object->contains("prototypes"))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.instance-set/1 is malformed", {}, {}, "asset.migration"));
        auto migrated             = *object;
        migrated["schemaVersion"] = Value(int64_t(2));
        migrated["prototypes"]    = Value(Value::Array{});
        Value next(std::move(migrated));
        return current.value() == 2 ? Result<Value>::success(std::move(next))
                                    : migrateDefinition(type, SchemaVersion(2), current, next);
    }
    if (type == "eve.instance-set" && from.value() == 2 && current.value() >= 3) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.instance-set" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 2 ||
            !object->contains("prototypes") || !object->at("prototypes").isArray())
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.instance-set/2 is malformed", {}, {}, "asset.migration"));
        auto migrated = *object;
        auto prototypes = *migrated["prototypes"].getIf<Value::Array>();
        for (auto& value : prototypes) {
            auto* prototype = value.getIf<Value::Object>();
            if (!prototype || prototype->contains("resourceAsset"))
                return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                "eve.instance-set/2 prototype is malformed", {}, {},
                                                                "asset.migration"));
            (*prototype)["resourceAsset"] = Value("");
        }
        migrated["prototypes"] = Value(std::move(prototypes));
        migrated["schemaVersion"] = Value(int64_t(3));
        Value next(std::move(migrated));
        return current.value() == 3 ? Result<Value>::success(std::move(next))
                                    : migrateDefinition(type, SchemaVersion(3), current, next);
    }
    if (type == "eve.instance-set" && from.value() == 3 && current.value() >= 4) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.instance-set" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 3 ||
            !object->contains("prototypes") || !object->at("prototypes").isArray())
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.instance-set/3 is malformed", {}, {}, "asset.migration"));
        auto migrated = *object;
        auto prototypes = *migrated["prototypes"].getIf<Value::Array>();
        for (auto& value : prototypes) {
            auto* prototype = value.getIf<Value::Object>();
            if (!prototype || prototype->contains("healthyColor") || prototype->contains("dryColor") ||
                prototype->contains("bendFactor") || prototype->contains("holeEdgePadding") ||
                prototype->contains("useDensityScaling"))
                return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                "eve.instance-set/3 prototype is malformed", {}, {},
                                                                "asset.migration"));
            (*prototype)["healthyColor"] = Value(Value::Array{Value(1.0), Value(1.0), Value(1.0), Value(1.0)});
            (*prototype)["dryColor"] = Value(Value::Array{Value(1.0), Value(1.0), Value(1.0), Value(1.0)});
            (*prototype)["bendFactor"] = Value(0.0);
            (*prototype)["holeEdgePadding"] = Value(0.0);
            (*prototype)["useDensityScaling"] = Value(true);
        }
        migrated["prototypes"] = Value(std::move(prototypes));
        migrated["schemaVersion"] = Value(int64_t(4));
        Value next(std::move(migrated));
        return current.value() == 4 ? Result<Value>::success(std::move(next))
                                    : migrateDefinition(type, SchemaVersion(4), current, next);
    }
    if (type == "eve.instance-set" && from.value() == 4 && current.value() == 5) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.instance-set" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 4 ||
            object->contains("wavingGrass"))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.instance-set/4 is malformed", {}, {}, "asset.migration"));
        auto migrated = *object;
        migrated["wavingGrass"] = Value(Value::Object{
            {"amount", Value(0.0)}, {"speed", Value(0.0)}, {"strength", Value(0.0)},
            {"tint", Value(Value::Array{Value(1.0), Value(1.0), Value(1.0), Value(1.0)})}});
        migrated["schemaVersion"] = Value(int64_t(5));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.material" && from.value() == 5 && current.value() == 6) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 5 ||
            object->contains("vegetationSurface"))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.material/5 is malformed or has unversioned vegetation surface", {}, {},
                "asset.migration"));
        auto migrated             = *object;
        migrated["schemaVersion"] = Value(int64_t(6));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.material" && from.value() == 6 && current.value() == 7) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 6)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError, "eve.material/6 is malformed",
                                                            {}, {}, "asset.migration"));
        auto migrated             = *object;
        migrated["schemaVersion"] = Value(int64_t(7));
        if (auto surface = migrated.find("vegetationSurface"); surface != migrated.end()) {
            auto* vegetation = surface->second.getIf<Value::Object>();
            if (!vegetation || vegetation->contains("colors"))
                return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                "eve.material/6 has unversioned global colors", {}, {},
                                                                "asset.migration"));
            (*vegetation)["colors"]                      = Value(1.0);
            (*vegetation)["colorsIntensity"]             = Value(1.0);
            (*vegetation)["colorsMask"]                  = Value(1.0);
            (*vegetation)["colorsVariation"]             = Value(.5);
            (*vegetation)["vertexOcclusionMinimum"]      = Value(0.0);
            (*vegetation)["vertexOcclusionMaximum"]      = Value(1.0);
            (*vegetation)["invertVertexOcclusionColors"] = Value(false);
        }
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.material" && from.value() == 7 && current.value() == 8) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 7 ||
            object->contains("vegetationDetail"))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.material/7 is malformed or has unversioned vegetation detail", {}, {},
                "asset.migration"));
        auto migrated             = *object;
        migrated["schemaVersion"] = Value(int64_t(8));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.material" && from.value() == 8 && current.value() == 9) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 8 ||
            (object->contains("vegetationAlpha") || object->contains("vegetationFields")))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.material/8 is malformed or has unversioned vegetation field data", {},
                {}, "asset.migration"));
        auto migrated             = *object;
        migrated["schemaVersion"] = Value(int64_t(9));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.material" && from.value() == 9 && current.value() == 10) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 9)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError, "eve.material/9 is malformed",
                                                            {}, {}, "asset.migration"));
        auto migrated = *object;
        if (auto at = migrated.find("vegetationFields"); at != migrated.end()) {
            auto* fields = at->second.getIf<Value::Object>();
            if (!fields || fields->size() != 4 || !fields->contains("colorsLayer") ||
                !fields->contains("colorsUsePivotPosition") || !fields->contains("extrasLayer") ||
                !fields->contains("extrasUsePivotPosition"))
                return Result<Value>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "eve.material/9 has malformed or unversioned vegetation fields", {}, {},
                    "asset.migration"));
            (*fields)["motionLayer"]   = Value(int64_t(0));
            (*fields)["vertexLayer"]   = Value(int64_t(0));
            (*fields)["globalSize"]    = Value(1.0);
            (*fields)["sizeFadeStart"] = Value(0.0);
            (*fields)["sizeFadeEnd"]   = Value(100.0);
        }
        migrated["schemaVersion"] = Value(int64_t(10));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.material" && from.value() == 10 && current.value() == 11) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 10 ||
            object->contains("vegetationMotion"))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.material/10 is malformed or has unversioned vegetation motion", {}, {},
                "asset.migration"));
        auto migrated             = *object;
        migrated["schemaVersion"] = Value(int64_t(11));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.material" && from.value() == 11 && current.value() == 12) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 11)
            return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError, "eve.material/11 is malformed",
                                                            {}, {}, "asset.migration"));
        auto migrated = *object;
        if (auto at = migrated.find("vegetationAlpha"); at != migrated.end()) {
            auto* alpha = at->second.getIf<Value::Object>();
            if (!alpha || alpha->size() != 3 || !alpha->contains("global") || !alpha->contains("variation") ||
                !alpha->contains("detailFade"))
                return Result<Value>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "eve.material/11 has malformed or unversioned vegetation alpha fade",
                    {}, {}, "asset.migration"));
            (*alpha)["glancing"] = Value(0.0);
            (*alpha)["camera"]   = Value(1.0);
            (*alpha)["constant"] = Value(0.0);
        }
        migrated["schemaVersion"] = Value(int64_t(12));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.material" && from.value() == 12 && current.value() == 13) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 12 ||
            object->contains("vegetationEmission"))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.material/12 is malformed or has unversioned vegetation emission", {},
                {}, "asset.migration"));
        auto migrated             = *object;
        migrated["schemaVersion"] = Value(int64_t(13));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.material" && from.value() == 13 && current.value() == 14) {
        const auto* object     = definition.getIf<Value::Object>();
        const auto* vegetation = object && object->contains("vegetationSurface")
                                     ? object->at("vegetationSurface").getIf<Value::Object>()
                                     : nullptr;
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 13 ||
            object->contains("vegetationGradient") || object->contains("cullMode") ||
            (vegetation &&
             (vegetation->contains("vertexOcclusionColor") || vegetation->contains("backfaceNormalMode"))))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError,
                "eve.material/13 is malformed or has unversioned vegetation gradient or occlusion color", {}, {},
                "asset.migration"));
        auto migrated             = *object;
        migrated["schemaVersion"] = Value(int64_t(14));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.material" && from.value() == 14 && current.value() == 15) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 14 ||
            object->contains("alphaToCoverage"))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.material/14 is malformed or has unversioned alpha-to-coverage", {}, {},
                "asset.migration"));
        auto migrated               = *object;
        migrated["schemaVersion"]   = Value(int64_t(15));
        migrated["alphaToCoverage"] = Value(false);
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.terrain-material" && from.value() == 2 && current.value() == 3) {
        const auto* object      = definition.getIf<Value::Object>();
        const auto* layersValue = object && object->contains("layers") ? &object->at("layers") : nullptr;
        const auto* layers      = layersValue ? layersValue->getIf<Value::Array>() : nullptr;
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.terrain-material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 2 || !layers ||
            object->contains("holesAsset") || object->contains("controlAssets"))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.terrain-material/2 is malformed or has unversioned AssetRefs", {}, {},
                "asset.migration"));
        auto migratedLayers = *layers;
        for (auto& encoded : migratedLayers) {
            auto* layer = encoded.getIf<Value::Object>();
            if (!layer || layer->contains("diffuseAsset") || layer->contains("normalAsset") ||
                layer->contains("weightAsset") || layer->contains("maskAsset"))
                return Result<Value>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "eve.terrain-material/2 layer has unversioned AssetRefs", {}, {},
                    "asset.migration"));
            (*layer)["diffuseAsset"] = Value("");
            (*layer)["normalAsset"]  = Value("");
            (*layer)["weightAsset"]  = Value("");
            (*layer)["maskAsset"]    = Value("");
        }
        auto migrated             = *object;
        migrated["schemaVersion"] = Value(int64_t(3));
        migrated["layers"]        = Value(std::move(migratedLayers));
        migrated["holesAsset"]    = Value("");
        migrated["controlAssets"] = Value(Value::Array{"", "", "", ""});
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.terrain-material" && from.value() == 1 && current.value() == 2) {
        const auto* object = definition.getIf<Value::Object>();
        const auto* layers =
            object && object->contains("layers") ? object->at("layers").getIf<Value::Array>() : nullptr;
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.terrain-material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 1 || !layers ||
            object->contains("holesSource") || object->contains("controlSources") ||
            object->contains("boundsMultiplier"))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.terrain-material/1 is malformed or has unversioned TVE fields", {}, {},
                "asset.migration"));
        auto migratedLayers = *layers;
        for (auto& encoded : migratedLayers) {
            auto* layer = encoded.getIf<Value::Object>();
            if (!layer || layer->contains("maskSource") || layer->contains("tileScaleMeters") ||
                layer->contains("tileOffsetMeters") || layer->contains("maskRemapMinimum") ||
                layer->contains("maskRemapMaximum") || layer->contains("specular") || layer->contains("metallic") ||
                layer->contains("normalScale") || layer->contains("smoothness"))
                return Result<Value>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "eve.terrain-material/1 layer has unversioned TVE fields", {}, {},
                    "asset.migration"));
            double tileSize = 1;
            if (auto found = layer->find("tileSizeMeters"); found != layer->end()) {
                if (!found->second.isNumeric())
                    return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                    "eve.terrain-material/1 tile size is invalid", {},
                                                                    {}, "asset.migration"));
                tileSize = found->second.isInt64() ? double(found->second.asInt()) : found->second.asDouble();
            }
            (*layer)["maskSource"]       = Value("");
            (*layer)["tileScaleMeters"]  = Value(Value::Array{tileSize, tileSize});
            (*layer)["tileOffsetMeters"] = Value(Value::Array{0.0, 0.0});
            (*layer)["maskRemapMinimum"] = Value(Value::Array{0.0, 0.0, 0.0, 0.0});
            (*layer)["maskRemapMaximum"] = Value(Value::Array{1.0, 1.0, 1.0, 1.0});
            (*layer)["specular"]         = Value(Value::Array{0.0, 0.0, 0.0, 0.0});
            (*layer)["metallic"]         = Value(0.0);
            (*layer)["normalScale"]      = Value(1.0);
            (*layer)["smoothness"]       = Value(0.0);
        }
        auto migrated                = *object;
        migrated["schemaVersion"]    = Value(int64_t(2));
        migrated["layers"]           = Value(std::move(migratedLayers));
        migrated["holesSource"]      = Value("");
        migrated["controlSources"]   = Value(Value::Array{"", "", "", ""});
        migrated["boundsMultiplier"] = Value(1.0);
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.material" && from.value() == 4 && current.value() == 5) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("schema") || !object->at("schema").isString() ||
            object->at("schema").asString() != "eve.material" || !object->contains("schemaVersion") ||
            !object->at("schemaVersion").isInt64() || object->at("schemaVersion").asInt() != 4 ||
            object->contains("motionHighlightColor"))
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "eve.material/4 is malformed or has unversioned motion highlight", {}, {},
                "asset.migration"));
        auto migrated             = *object;
        migrated["schemaVersion"] = Value(int64_t(5));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    if (type == "eve.image" && from.value() == 2 && current.value() == 3) return migrateImageV2ToV3(definition);
    if (type == "eve.scene-template" && from.value() == 2 && current.value() == 3) {
        const auto* object = definition.getIf<Value::Object>();
        if (!object || !object->contains("nodes") || !object->contains("renderers") || !object->contains("schema") ||
            !object->at("schema").isString() || object->at("schema").asString() != "eve.scene-template" ||
            !object->contains("schemaVersion") || !object->at("schemaVersion").isInt64() ||
            object->at("schemaVersion").asInt() != 2 || !object->at("renderers").isArray())
            return Result<Value>::failure(Diagnostic::error(
                DiagnosticCode::ParseError, "scene-template/2 is malformed or has unversioned shadow state", {}, {},
                "asset.migration"));
        auto migrated             = *object;
        migrated["schemaVersion"] = Value(std::int64_t(3));
        auto renderers = *migrated["renderers"].getIf<Value::Array>();
        for (auto& renderer : renderers) {
            auto* binding = renderer.getIf<Value::Object>();
            if (!binding || binding->contains("castShadows") || binding->contains("receiveShadows"))
                return Result<Value>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                "scene-template/2 renderer shadow state is malformed",
                                                                {}, {}, "asset.migration"));
            (*binding)["castShadows"]    = Value(true);
            (*binding)["receiveShadows"] = Value(true);
        }
        migrated["renderers"] = Value(std::move(renderers));
        return Result<Value>::success(Value(std::move(migrated)));
    }
    return Result<Value>::failure(Diagnostic::error(DiagnosticCode::Unsupported,
                                                    "asset definition has no registered migration", std::string(type),
                                                    {}, "asset.migration"));
}

Result<void> refreshImportReport(EvaArchive& archive) {
    const auto path = archive.manifest.provenance.find("path");
    if (path == archive.manifest.provenance.end()) return Result<void>::success();
    if (!path->second.isString() || path->second.asString() != "reports/import.json")
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "migration found an invalid import report path", {}, {}, "asset.migration"));
    const auto entry = std::lower_bound(
        archive.entries.begin(), archive.entries.end(), path->second.asString(),
        [](const EvaArchiveEntry& candidate, std::string_view value) { return candidate.path < value; });
    if (entry == archive.entries.end() || entry->path != path->second.asString())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "migration import report is missing",
                                                       path->second.asString(), {}, "asset.migration"));
    auto parsed = Value::fromJson(std::string(entry->bytes.begin(), entry->bytes.end()));
    if (!parsed) return Result<void>::failure(parsed.status());
    auto* report = parsed.value().getIf<Value::Object>();
    if (!report)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::ParseError, "migration import report root is invalid", entry->path, {}, "asset.migration"));
    Value::Array assets;
    for (const auto& asset : archive.manifest.assets)
        assets.emplace_back(
            Value::Object{{"asset", Value(asset.asset.format())},
                          {"type", Value(asset.type + "/" + std::to_string(asset.schemaVersion.value()))},
                          {"contentHash", Value(asset.contentHash)}});
    (*report)["canonicalAssets"] = Value(std::move(assets));
    report->erase("importKey");
    auto facts = parsed.value().toJson();
    if (!facts) return Result<void>::failure(facts.status());
    const std::string key  = sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(facts.value().data()), facts.value().size()));
    (*report)["importKey"] = Value(key);
    auto encoded           = parsed.value().toJson();
    if (!encoded) return Result<void>::failure(encoded.status());
    entry->bytes.assign(encoded.value().begin(), encoded.value().end());
    archive.manifest.provenance["importKey"] = Value(key);
    return Result<void>::success();
}

}  // namespace

Result<SchemaVersion> currentAssetSchemaVersion(std::string_view type) {
    static const std::map<std::string_view, std::uint64_t> versions = {
        {"eve.image", 3},
        {"eve.volume-texture", 1},
        {"eve.texture", 1},
        {"eve.mesh", 3},
        {"eve.skeleton", 1},
        {"eve.animation-clip", 1},
        {"eve.skin", 1},
        {"eve.material", 15},
        {"eve.scene-template", 3},
        {"eve.terrain", 1},
        {"eve.terrain-material", 3},
        {"eve.pcg-graph", 1},
        {"eve.instance-set", 5},
        {"eve.audio", 1},
        {"eve.font", 1},
        {"eve.sprite-animation", 1},
        {"eve.shader", 1},
        {"eve.stylize.mesh-vfx", 1},
        {"eve.vegetation-conversion-preset", 1},
        {"eve.vegetation-scene", 1},
    };
    const auto found = versions.find(type);
    if (found == versions.end())
        return Result<SchemaVersion>::failure(Diagnostic::error(
            DiagnosticCode::Unsupported, "unknown canonical asset type", std::string(type), {}, "asset.migration"));
    return Result<SchemaVersion>::success(SchemaVersion(found->second));
}

Result<EvaArchive> migrateEvaArchive(EvaArchive source, const EvaArchiveLimits& limits) {
    for (auto& asset : source.manifest.assets) {
        auto current = currentAssetSchemaVersion(asset.type);
        if (!current) return Result<EvaArchive>::failure(current.status());
        if (asset.schemaVersion.value() > current.value().value())
            return Result<EvaArchive>::failure(Diagnostic::error(DiagnosticCode::UnknownVersion,
                                                                 "asset definition is newer than this reader",
                                                                 asset.type, {}, "asset.migration"));
        const auto entry = std::lower_bound(
            source.entries.begin(), source.entries.end(), asset.definition,
            [](const EvaArchiveEntry& candidate, std::string_view path) { return candidate.path < path; });
        if (entry == source.entries.end() || entry->path != asset.definition)
            return Result<EvaArchive>::failure(Diagnostic::error(
                DiagnosticCode::NotFound, "asset definition is missing", asset.definition, {}, "asset.migration"));
        const std::string_view text(reinterpret_cast<const char*>(entry->bytes.data()), entry->bytes.size());
        auto                   parsed = Value::fromJson(text);
        if (!parsed) return Result<EvaArchive>::failure(parsed.status());
        auto migrated = migrateDefinition(asset.type, asset.schemaVersion, current.value(), parsed.value());
        if (!migrated) return Result<EvaArchive>::failure(migrated.status());
        auto encoded = migrated.value().toJson();
        if (!encoded) return Result<EvaArchive>::failure(encoded.status());
        entry->bytes.assign(encoded.value().begin(), encoded.value().end());
        if (asset.schemaVersion != current.value()) {
            const std::string oldType = asset.type + "/" + std::to_string(asset.schemaVersion.value());
            const std::string newType = asset.type + "/" + std::to_string(current.value().value());
            for (auto& dependency : source.manifest.dependencies)
                if (dependency.to.id() == asset.asset.id() && dependency.expectedType == oldType)
                    dependency.expectedType = newType;
        }
        asset.schemaVersion = current.value();
        asset.contentHash   = sha256(entry->bytes);
    }
    auto report = refreshImportReport(source);
    if (!report) return Result<EvaArchive>::failure(report.status());
    auto rebuilt = buildEvaArchive(source.manifest, std::move(source.entries), limits);
    if (!rebuilt) return Result<EvaArchive>::failure(rebuilt.status());
    return parseEvaArchive(rebuilt.value(), limits);
}

}  // namespace eve::asset
