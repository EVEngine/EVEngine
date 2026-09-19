#include "asset/graphics/EvpackVegetationScene.h"

#include "asset/RuntimeDefinition.h"
#include "asset/EvpackImageDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <limits>
#include <new>
#include <numeric>
#include <set>

namespace eve::asset_graphics {
namespace {
const Value& member(const Value::Object& object, std::string_view name) { return object.at(std::string(name)); }

bool exact(const Value::Object& object, std::initializer_list<std::string_view> fields) {
    if (object.size() != fields.size()) return false;
    return std::all_of(fields.begin(), fields.end(), [&](auto name) { return object.contains(std::string(name)); });
}

bool numeric(const Value& value, float& result) {
    if (!value.isNumeric()) return false;
    const double decoded = value.isInt64() ? double(value.asInt()) : value.asDouble();
    if (!std::isfinite(decoded) || decoded < -std::numeric_limits<float>::max() ||
        decoded > std::numeric_limits<float>::max())
        return false;
    result = float(decoded);
    return true;
}

template <std::size_t N>
bool values(const Value& value, std::array<float, N>& result) {
    const auto* array = value.getIf<Value::Array>();
    if (!array || array->size() != N) return false;
    for (std::size_t index = 0; index < N; ++index)
        if (!numeric((*array)[index], result[index])) return false;
    return true;
}

bool validGuid(const Value& value, std::string& result) {
    if (!value.isString()) return false;
    result = value.asString();
    return result.empty() ||
           (result.size() == 32 && std::all_of(result.begin(), result.end(), [](unsigned char c) {
                return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
            }));
}

bool unit(float value) { return value >= 0 && value <= 1; }
bool nonnegative(float value) { return value >= 0 && std::isfinite(value); }
bool whole(float value) { return std::trunc(value) == value; }

bool finite(glm::vec2 value) { return std::isfinite(value.x) && std::isfinite(value.y); }
bool finite(glm::vec4 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && std::isfinite(value.w);
}

float remap(float value, float minimum, float maximum, float epsilon) {
    value = std::clamp(value, .0001f, .9999f);
    const float denominator = maximum - minimum + epsilon;
    if (denominator == 0.f) return value >= maximum ? 1.f : 0.f;
    return std::clamp((value - minimum) / denominator, 0.f, 1.f);
}

glm::vec3 rotate(const std::array<float, 4>& rotation, glm::vec3 value) {
    const glm::vec3 xyz(rotation[0], rotation[1], rotation[2]);
    const glm::vec3 t = 2.f * glm::cross(xyz, value);
    return value + rotation[3] * t + glm::cross(xyz, t);
}

glm::vec3 inverseRotate(const std::array<float, 4>& rotation, glm::vec3 value) {
    const std::array<float, 4> inverse{-rotation[0], -rotation[1], -rotation[2], rotation[3]};
    return rotate(inverse, value);
}

glm::vec4 sampleMask(const graphics::VegetationMask& mask, glm::vec2 uv) {
    if (mask.pixels.empty()) return glm::vec4(1.f);
    const float x = std::clamp(uv.x * mask.width - .5f, 0.f, float(mask.width - 1));
    const float y = std::clamp(uv.y * mask.height - .5f, 0.f, float(mask.height - 1));
    const auto x0 = std::uint32_t(x), y0 = std::uint32_t(y);
    const auto x1 = std::min(x0 + 1, mask.width - 1), y1 = std::min(y0 + 1, mask.height - 1);
    return glm::mix(glm::mix(mask.pixels[std::size_t(y0) * mask.width + x0],
                             mask.pixels[std::size_t(y0) * mask.width + x1], x - float(x0)),
                    glm::mix(mask.pixels[std::size_t(y1) * mask.width + x0],
                             mask.pixels[std::size_t(y1) * mask.width + x1], x - float(x0)),
                    y - float(y0));
}

glm::vec4 seasonal(const VegetationSceneElement& element, float season) {
    if (!element.seasonal) return glm::vec4(element.value[0], element.value[1], element.value[2], element.value[3]);
    const float wrapped = season == 4.f ? 0.f : season;
    const auto index = std::uint32_t(wrapped);
    const float fraction = wrapped - float(index);
    const float smooth = fraction * fraction * (3.f - 2.f * fraction);
    const auto& a = element.seasons[index];
    const auto& b = element.seasons[(index + 1) % 4];
    return glm::mix(glm::vec4(a[0], a[1], a[2], a[3]), glm::vec4(b[0], b[1], b[2], b[3]), smooth);
}

float propertyScalar(const VegetationSceneElement& element, std::string_view name, float fallback) {
    const auto found = std::find_if(element.properties.begin(), element.properties.end(),
                                    [&](const auto& property) { return property.name == name; });
    return found == element.properties.end() ? fallback : found->value;
}

glm::vec4 propertyVector(const VegetationSceneElement& element, std::string_view name, glm::vec4 fallback) {
    const auto found = std::find_if(element.properties.begin(), element.properties.end(),
                                    [&](const auto& property) { return property.name == name; });
    if (found == element.properties.end()) return fallback;
    return {found->vector[0], found->vector[1], found->vector[2], found->vector[3]};
}
}  // namespace

Result<LoadedVegetationScene> EvpackVegetationSceneLoader::load(
    const AssetRef& asset, const asset::EvpackCapabilities& capabilities, std::uint64_t maximumDecodedBytes) const {
    auto payload = reader_.read(asset, "eve.vegetation-scene/1", capabilities, maximumDecodedBytes);
    if (!payload) return Result<LoadedVegetationScene>::failure(payload.status());
    const asset::RuntimeAssetChunk* definition = nullptr;
    for (const auto& chunk : payload.value().chunks) {
        if (chunk.kind != asset::EvpackChunkKind::Definition) continue;
        if (definition)
            return Result<LoadedVegetationScene>::failure(Diagnostic::error(
                DiagnosticCode::Conflict, "duplicate scene definition", {}, {}, "asset.graphics.vegetation-scene"));
        definition = &chunk;
    }
    if (!definition)
        return Result<LoadedVegetationScene>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "scene definition is missing", {}, {}, "asset.graphics.vegetation-scene"));
    asset::RuntimeDefinitionLimits limits;
    limits.maximumBytes = maximumDecodedBytes;
    auto decoded = asset::decodeRuntimeDefinition(definition->bytes, limits);
    if (!decoded) return Result<LoadedVegetationScene>::failure(decoded.status());
    const auto* root = decoded.value().getIf<Value::Object>();
    if (!root || !exact(*root, {"schema", "schemaVersion", "sourceGuid", "control", "details", "motion", "volume", "elements"}) ||
        !member(*root, "schema").isString() || member(*root, "schema").asString() != "eve.vegetation-scene" ||
        !member(*root, "schemaVersion").isInt64() || member(*root, "schemaVersion").asInt() != 1 ||
        !member(*root, "sourceGuid").isString())
        return Result<LoadedVegetationScene>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                        "vegetation scene root is malformed", {}, {},
                                                                        "asset.graphics.vegetation-scene"));
    std::string sourceGuid;
    if (!validGuid(member(*root, "sourceGuid"), sourceGuid) || sourceGuid.empty())
        return Result<LoadedVegetationScene>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                        "vegetation scene source GUID is invalid", {},
                                                                        {}, "asset.graphics.vegetation-scene"));

    const auto* control = member(*root, "control").getIf<Value::Object>();
    const auto* details = member(*root, "details").getIf<Value::Object>();
    const auto* motion = member(*root, "motion").getIf<Value::Object>();
    const auto* volume = member(*root, "volume").getIf<Value::Object>();
    if (!control || !details || !motion || !volume ||
        !exact(*control, {"values", "globalColor", "overlayColor", "overlayAlbedoGuid", "overlayNormalGuid",
                          "noiseTextureGuid"}) ||
        !exact(*details, {"layers", "global", "colorMask", "overlayMask", "alphaThreshold", "perspective",
                          "motionHighlight", "bending", "flutter", "interactionAmplitude"}) ||
        !exact(*motion, {"values", "direction", "noiseTextureGuid"}) ||
        !exact(*volume, {"values", "colors", "extras", "motion", "vertex"}))
        return Result<LoadedVegetationScene>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                        "vegetation scene sections are malformed", {},
                                                                        {}, "asset.graphics.vegetation-scene"));

    LoadedVegetationScene result{asset, std::move(sourceGuid), payload.value().variant, {}, {}, {}, {}, {}};
    std::array<float, 17> controls{};
    std::array<float, 8> motions{};
    std::array<float, 4> volumes{};
    std::array<float, 2> direction{};
    if (!values(member(*control, "values"), controls) || !values(member(*control, "globalColor"), result.control.globalColor) ||
        !values(member(*control, "overlayColor"), result.control.overlayColor) ||
        !values(member(*motion, "values"), motions) || !values(member(*motion, "direction"), direction) ||
        !values(member(*volume, "values"), volumes) ||
        !validGuid(member(*control, "overlayAlbedoGuid"), result.control.overlayAlbedoGuid) ||
        !validGuid(member(*control, "overlayNormalGuid"), result.control.overlayNormalGuid) ||
        !validGuid(member(*control, "noiseTextureGuid"), result.control.noiseTextureGuid) ||
        !validGuid(member(*motion, "noiseTextureGuid"), result.motion.noiseTextureGuid))
        return Result<LoadedVegetationScene>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                        "vegetation scene values are malformed", {}, {},
                                                                        "asset.graphics.vegetation-scene"));
    auto& c = result.control;
    c.season = controls[0]; c.globalAlpha = controls[1]; c.globalOverlay = controls[2];
    c.globalWetness = controls[3]; c.globalEmissive = controls[4]; c.globalSubsurface = controls[5];
    c.globalSize = controls[6]; c.overlaySmoothness = controls[7]; c.overlayNormalScale = controls[8];
    c.overlaySubsurface = controls[9]; c.overlayScale = controls[10]; c.wetnessContrast = controls[11];
    c.wetnessNormalScale = controls[12]; c.noiseTiling = controls[13]; c.proximityFade = controls[14];
    c.distanceFadeBias = controls[15]; c.defaultConformHeight = controls[16];
    auto& m = result.motion;
    m.windPower = motions[0]; m.noiseTiling = motions[1]; m.bending = motions[2]; m.branch = motions[3];
    m.flutter = motions[4]; m.speed = motions[5]; m.animatedTime = motions[6] >= .5f; m.fadeDistance = motions[7];
    m.direction = direction;
    if (!whole(volumes[1]) || !whole(volumes[2]))
        return Result<LoadedVegetationScene>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                        "vegetation volume policy enum is not integral",
                                                                        {}, {}, "asset.graphics.vegetation-scene"));
    result.volume.renderScale = volumes[0]; result.volume.edgeFade = volumes[3];
    result.volume.visibility = std::uint32_t(volumes[1]); result.volume.sorting = std::uint32_t(volumes[2]);
    const std::array<std::string_view, 4> channelNames{"colors", "extras", "motion", "vertex"};
    for (std::size_t index = 0; index < channelNames.size(); ++index) {
        std::array<float, 3> channel{};
        if (!values(member(*volume, channelNames[index]), channel))
            return Result<LoadedVegetationScene>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                            "vegetation volume channel is malformed",
                                                                            {}, {}, "asset.graphics.vegetation-scene"));
        if (!whole(channel[0]) || !whole(channel[1]) || !whole(channel[2]) || channel[1] < 0 || channel[2] < 0)
            return Result<LoadedVegetationScene>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation volume channel integers are invalid", {},
                                  {}, "asset.graphics.vegetation-scene"));
        result.volume.channels[index] = {std::int32_t(channel[0]), std::uint32_t(channel[1]),
                                         std::uint32_t(channel[2])};
    }
    if (c.season < 0 || c.season > 4 || !unit(c.globalAlpha) || !unit(c.globalOverlay) || !unit(c.globalWetness) ||
        !nonnegative(c.globalEmissive) || !unit(c.globalSubsurface) || !unit(c.globalSize) ||
        !unit(c.overlaySmoothness) || !unit(c.overlayNormalScale) || !unit(c.overlaySubsurface) || c.overlayScale <= 0 ||
        !unit(c.wetnessContrast) || !unit(c.wetnessNormalScale) || c.noiseTiling <= 0 || !nonnegative(c.proximityFade) ||
        !nonnegative(c.distanceFadeBias) || !unit(c.globalColor[3]) || !unit(c.overlayColor[3]) ||
        !std::all_of(c.globalColor.begin(), c.globalColor.begin() + 3, nonnegative) ||
        !std::all_of(c.overlayColor.begin(), c.overlayColor.begin() + 3, nonnegative) || !unit(m.windPower) ||
        m.noiseTiling <= 0 || m.bending < 0 || m.bending > 2 || m.branch < 0 || m.branch > 2 ||
        m.flutter < 0 || m.flutter > 2 || !nonnegative(m.speed) || !nonnegative(m.fadeDistance) ||
        result.volume.renderScale <= 0 || !unit(result.volume.edgeFade) ||
        (result.volume.visibility != 0 && result.volume.visibility != 10 && result.volume.visibility != 20) ||
        (result.volume.sorting != 0 && result.volume.sorting != 10 && result.volume.sorting != 20) ||
        std::abs(std::hypot(m.direction[0], m.direction[1]) - 1.f) > 1e-4f ||
        !std::all_of(result.volume.channels.begin(), result.volume.channels.end(), [](const auto& channel) {
            return (channel.renderMode == -1 || channel.renderMode == 10 || channel.renderMode == 20) &&
                   channel.width >= 32 && channel.height >= 32 && channel.width <= 16384 && channel.height <= 16384;
        }))
        return Result<LoadedVegetationScene>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                        "vegetation scene values are out of range", {},
                                                                        {}, "asset.graphics.vegetation-scene"));

    Value detailDocument = Value::object({{"schema", "eve.graphics.vegetation-details"}, {"version", 1},
        {"layers", member(*details, "layers")}, {"global", member(*details, "global")},
        {"colorMask", member(*details, "colorMask")}, {"overlayMask", member(*details, "overlayMask")},
        {"alphaThreshold", member(*details, "alphaThreshold")}, {"perspective", member(*details, "perspective")},
        {"motionHighlight", member(*details, "motionHighlight")}, {"bending", member(*details, "bending")},
        {"flutter", member(*details, "flutter")}, {"interactionAmplitude", member(*details, "interactionAmplitude")}});
    auto restored = graphics::restoreVegetationDetails(detailDocument);
    if (!restored) return Result<LoadedVegetationScene>::failure(restored.status());
    result.details = std::move(restored).takeValue();

    const auto* elementValues = member(*root, "elements").getIf<Value::Array>();
    if (!elementValues || elementValues->size() > 4096)
        return Result<LoadedVegetationScene>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                        "vegetation scene element count is invalid", {},
                                                                        {}, "asset.graphics.vegetation-scene"));
    result.elements.reserve(elementValues->size());
    const std::set<std::string> kinds{
        "color-effect", "color-map", "color-noise", "color-tint", "extras-alpha", "extras-emissive",
        "extras-overlay", "extras-wetness", "motion-advanced", "motion-interaction", "motion-wind-power",
        "vertex-conform-model", "vertex-conform-simple", "vertex-conform-terrain", "vertex-height-offset",
        "vertex-height", "vertex-orientation-model", "vertex-orientation-terrain", "vertex-size"};
    for (const auto& encoded : *elementValues) {
        const auto* element = encoded.getIf<Value::Object>();
        if (!element || !exact(*element, {"sourceFileId", "shaderGuid", "kind", "channel", "properties", "enabled", "visibility", "position",
                                          "rotation", "scale", "layers", "intensity", "value", "seasonal",
                                          "seasons", "textureGuid", "textureAsset", "remap", "blendRgb", "blendAlpha", "directionMode",
                                          "invertDirection", "volumeFade", "motionMode", "motionPower"}))
            return Result<LoadedVegetationScene>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                            "vegetation scene element is malformed", {},
                                                                            {}, "asset.graphics.vegetation-scene"));
        VegetationSceneElement decodedElement;
        const auto& sourceFileId = member(*element, "sourceFileId");
        const auto& shaderGuid = member(*element, "shaderGuid");
        const auto& kind = member(*element, "kind");
        const auto& channel = member(*element, "channel");
        const auto& enabled = member(*element, "enabled");
        const auto& visibility = member(*element, "visibility");
        const auto& layers = member(*element, "layers");
        const auto& seasonal = member(*element, "seasonal");
        const auto& blendRgb = member(*element, "blendRgb");
        const auto& blendAlpha = member(*element, "blendAlpha");
        const auto& directionMode = member(*element, "directionMode");
        const auto& invertDirection = member(*element, "invertDirection");
        const auto& volumeFade = member(*element, "volumeFade");
        const auto& motionMode = member(*element, "motionMode");
        if (!sourceFileId.isInt64() || sourceFileId.asInt() == 0 || !kind.isString() || !kinds.contains(kind.asString()) ||
            !channel.isInt64() || !enabled.isBool() || !visibility.isInt64() || !layers.isInt64() ||
            !seasonal.isBool() || !blendRgb.isInt64() || !blendAlpha.isInt64() || !directionMode.isInt64() || !invertDirection.isBool() ||
            !volumeFade.isBool() || !motionMode.isInt64() || !validGuid(shaderGuid, decodedElement.shaderGuid) ||
            decodedElement.shaderGuid.empty() ||
            !values(member(*element, "position"), decodedElement.position) ||
            !values(member(*element, "rotation"), decodedElement.rotation) ||
            !values(member(*element, "scale"), decodedElement.scale) ||
            !values(member(*element, "value"), decodedElement.value) ||
            !values(member(*element, "remap"), decodedElement.remap) ||
            !numeric(member(*element, "intensity"), decodedElement.intensity) ||
            !numeric(member(*element, "motionPower"), decodedElement.motionPower) ||
            !validGuid(member(*element, "textureGuid"), decodedElement.textureGuid) ||
            !member(*element, "textureAsset").isString())
            return Result<LoadedVegetationScene>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "vegetation scene element values are malformed", {}, {},
                                  "asset.graphics.vegetation-scene"));
        decodedElement.textureAsset = member(*element, "textureAsset").asString();
        if (!decodedElement.textureAsset.empty() && !AssetRef::parse(decodedElement.textureAsset))
            return Result<LoadedVegetationScene>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "vegetation scene element texture asset is invalid", {},
                                  {}, "asset.graphics.vegetation-scene"));
        const auto* propertyValues = member(*element, "properties").getIf<Value::Array>();
        if (!propertyValues || propertyValues->size() > 256)
            return Result<LoadedVegetationScene>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation scene element property count is invalid",
                                  {}, {}, "asset.graphics.vegetation-scene"));
        decodedElement.properties.reserve(propertyValues->size());
        std::set<std::string> propertyNames;
        for (const auto& encodedProperty : *propertyValues) {
            const auto* property = encodedProperty.getIf<Value::Object>();
            VegetationSceneElementProperty decodedProperty;
            if (!property || !exact(*property, {"name", "type", "textureGuid", "textureAsset", "vector", "value"}) ||
                !member(*property, "name").isString() || member(*property, "name").asString().empty() ||
                member(*property, "name").asString().size() > 128 || !member(*property, "type").isInt64() ||
                member(*property, "type").asInt() < 0 || member(*property, "type").asInt() > 2 ||
                !validGuid(member(*property, "textureGuid"), decodedProperty.textureGuid) ||
                !member(*property, "textureAsset").isString() ||
                !values(member(*property, "vector"), decodedProperty.vector) ||
                !numeric(member(*property, "value"), decodedProperty.value) ||
                !propertyNames.emplace(member(*property, "name").asString()).second)
                return Result<LoadedVegetationScene>::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "vegetation scene element property is malformed", {},
                                      {}, "asset.graphics.vegetation-scene"));
            decodedProperty.name = member(*property, "name").asString();
            decodedProperty.type = std::int32_t(member(*property, "type").asInt());
            decodedProperty.textureAsset = member(*property, "textureAsset").asString();
            if (!decodedProperty.textureAsset.empty() && !AssetRef::parse(decodedProperty.textureAsset))
                return Result<LoadedVegetationScene>::failure(Diagnostic::error(
                    DiagnosticCode::ParseError, "vegetation scene element property texture asset is invalid", {}, {},
                    "asset.graphics.vegetation-scene"));
            decodedElement.properties.push_back(std::move(decodedProperty));
        }
        const auto* seasonValues = member(*element, "seasons").getIf<Value::Array>();
        if (!seasonValues || seasonValues->size() != 4)
            return Result<LoadedVegetationScene>::failure(Diagnostic::error(DiagnosticCode::ParseError,
                                                                            "vegetation scene seasons are malformed",
                                                                            {}, {}, "asset.graphics.vegetation-scene"));
        for (std::size_t index = 0; index < 4; ++index)
            if (!values((*seasonValues)[index], decodedElement.seasons[index]))
                return Result<LoadedVegetationScene>::failure(
                    Diagnostic::error(DiagnosticCode::ParseError, "vegetation scene season is malformed", {}, {},
                                      "asset.graphics.vegetation-scene"));
        const auto rawChannel = channel.asInt(), rawVisibility = visibility.asInt(), rawLayers = layers.asInt();
        const auto rawBlendRgb = blendRgb.asInt(), rawBlendAlpha = blendAlpha.asInt();
        const auto rawDirectionMode = directionMode.asInt(), rawMotionMode = motionMode.asInt();
        if (rawChannel < 0 || rawChannel > 3 || rawVisibility < -1 || rawVisibility > 20 || rawLayers < 1 ||
            rawLayers > 0x1ff || rawBlendRgb < 0 || rawBlendRgb > 2 || rawBlendAlpha < 0 || rawBlendAlpha > 1 ||
            rawDirectionMode < 10 || rawDirectionMode > 40 ||
            rawMotionMode < 13 || rawMotionMode > 15)
            return Result<LoadedVegetationScene>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation scene element integers are out of range",
                                  {}, {}, "asset.graphics.vegetation-scene"));
        decodedElement.sourceFileId = sourceFileId.asInt(); decodedElement.kind = kind.asString();
        decodedElement.channel = std::uint32_t(rawChannel); decodedElement.enabled = enabled.asBool();
        decodedElement.visibility = std::int32_t(rawVisibility); decodedElement.layers = std::uint16_t(rawLayers);
        decodedElement.seasonal = seasonal.asBool(); decodedElement.blendRgb = std::int32_t(rawBlendRgb);
        decodedElement.blendAlpha = std::int32_t(rawBlendAlpha);
        decodedElement.directionMode = std::int32_t(directionMode.asInt());
        decodedElement.invertDirection = invertDirection.asBool(); decodedElement.volumeFade = volumeFade.asBool();
        decodedElement.motionMode = std::int32_t(motionMode.asInt());
        const float rotationLength = std::sqrt(std::inner_product(decodedElement.rotation.begin(), decodedElement.rotation.end(),
                                                                  decodedElement.rotation.begin(), 0.f));
        const std::uint32_t expectedChannel = decodedElement.kind.starts_with("color-") ? 0u :
            decodedElement.kind.starts_with("extras-") ? 1u : decodedElement.kind.starts_with("motion-") ? 2u : 3u;
        if (decodedElement.channel > 3 || decodedElement.channel != expectedChannel ||
            (decodedElement.visibility != -1 && decodedElement.visibility != 0 && decodedElement.visibility != 10 &&
             decodedElement.visibility != 20) || decodedElement.layers == 0 || (decodedElement.layers & ~0x1ffu) ||
            !unit(decodedElement.intensity) || !unit(decodedElement.motionPower) ||
            std::abs(rotationLength - 1.f) > 1e-3f ||
            std::any_of(decodedElement.scale.begin(), decodedElement.scale.end(), [](float value) { return value <= 0; }) ||
            decodedElement.remap[0] > decodedElement.remap[1] || decodedElement.remap[2] > decodedElement.remap[3] ||
            decodedElement.remap[4] > decodedElement.remap[5] ||
            (decodedElement.blendRgb < 0 || decodedElement.blendRgb > 2) ||
            (decodedElement.blendAlpha < 0 || decodedElement.blendAlpha > 1) ||
            (decodedElement.directionMode != 10 && decodedElement.directionMode != 20 &&
             decodedElement.directionMode != 30 && decodedElement.directionMode != 40) ||
            (decodedElement.motionMode != 13 && decodedElement.motionMode != 15))
            return Result<LoadedVegetationScene>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                            "vegetation scene element is out of range",
                                                                            {}, {}, "asset.graphics.vegetation-scene"));
        result.elements.push_back(std::move(decodedElement));
    }
    return Result<LoadedVegetationScene>::success(std::move(result));
}

Result<VegetationSceneProjection> projectVegetationScene(const graphics::PbrSurface& baseSurface,
                                                          const graphics::VegetationMotion& baseMotion,
                                                          const LoadedVegetationScene& scene) {
    auto detailed = graphics::configureVegetationDetails(baseSurface, baseMotion, scene.details);
    if (!detailed) return Result<VegetationSceneProjection>::failure(detailed.status());
    VegetationSceneProjection result{std::move(detailed.value().surface), std::move(detailed.value().motion),
                                     scene.volume, scene.control.season};
    const auto& c = scene.control;
    auto& s = result.surface;
    s.vegetationColor.fieldColor = c.globalColor;
    s.vegetationColor.overlay *= c.globalOverlay;
    s.vegetationColor.wetness *= c.globalWetness;
    s.vegetationColor.overlayColor = {c.overlayColor[0], c.overlayColor[1], c.overlayColor[2]};
    s.vegetationColor.overlaySmoothness = c.overlaySmoothness;
    s.vegetationColor.overlayNormalScale = c.overlayNormalScale;
    s.vegetationColor.overlaySubsurface = c.overlaySubsurface;
    s.vegetationColor.wetnessContrast = c.wetnessContrast;
    s.vegetationColor.wetnessNormalScale = c.wetnessNormalScale;
    s.vegetationAlpha.enabled = true;
    s.vegetationAlpha.global *= c.globalAlpha;
    s.vegetationAlpha.cameraFadeMin = (c.proximityFade + .01f) * .5f;
    s.vegetationAlpha.cameraFadeMax = c.proximityFade + .01f;
    if (s.vegetationEmission.enabled) s.vegetationEmission.global *= c.globalEmissive;
    s.translucency.globalIntensity *= c.globalSubsurface;
    s.vegetationVertex.globalSize *= c.globalSize;
    s.vegetationVertex.distanceFadeBias *= c.distanceFadeBias + .01f;
    auto& gpu = s.vegetationMotion;
    gpu.fallback[2] = scene.motion.windPower;
    gpu.globalDirection = scene.motion.direction;
    gpu.globalBending *= scene.motion.bending;
    gpu.globalBranch *= scene.motion.branch;
    gpu.globalFlutter *= scene.motion.flutter;
    gpu.noiseTiling *= scene.motion.noiseTiling;
    gpu.fadeDistance = scene.motion.fadeDistance;
    auto& cpu = result.motion;
    cpu.globalBending *= scene.motion.bending;
    cpu.globalBranch *= scene.motion.branch;
    cpu.globalFlutter *= scene.motion.flutter;
    cpu.noiseTiling *= scene.motion.noiseTiling;
    cpu.fadeDistance = scene.motion.fadeDistance;
    cpu.timeScale *= scene.motion.speed;
    cpu.globalSize *= c.globalSize;
    cpu.distanceFadeBias *= c.distanceFadeBias + .01f;
    auto valid = graphics::validatePbrSurface(s);
    if (!valid) return Result<VegetationSceneProjection>::failure(valid.status());
    return Result<VegetationSceneProjection>::success(std::move(result));
}

Result<std::map<std::string, graphics::VegetationMask>> loadVegetationSceneElementMasks(
    const asset::EvpackResourceReader& reader, const LoadedVegetationScene& scene,
    const asset::EvpackCapabilities& capabilities, std::uint64_t maximumDecodedBytes) {
    try {
    std::map<std::string, std::string> required;
    auto admit = [&](const std::string& guid, const std::string& asset) -> Result<void> {
        if (guid.empty()) return Result<void>::success();
        if (asset.empty())
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound,
                                                           "vegetation element texture dependency is unavailable", guid,
                                                           {}, "asset.graphics.vegetation-scene"));
        const auto [position, inserted] = required.emplace(guid, asset);
        if (!inserted && position->second != asset)
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict,
                                                           "vegetation element texture GUID has conflicting assets",
                                                           guid, {}, "asset.graphics.vegetation-scene"));
        return Result<void>::success();
    };
    for (const auto& element : scene.elements) {
        auto admitted = admit(element.textureGuid, element.textureAsset);
        if (!admitted) return Result<std::map<std::string, graphics::VegetationMask>>::failure(admitted.status());
        for (const auto& property : element.properties) {
            if (property.type != 0 || (property.name != "_MainTex" && property.name != "_NoiseTex")) continue;
            admitted = admit(property.textureGuid, property.textureAsset);
            if (!admitted) return Result<std::map<std::string, graphics::VegetationMask>>::failure(admitted.status());
        }
    }
    std::map<std::string, graphics::VegetationMask> output;
    std::uint64_t usedBytes = 0;
    for (const auto& [guid, encodedAsset] : required) {
        if (usedBytes >= maximumDecodedBytes)
            return Result<std::map<std::string, graphics::VegetationMask>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "vegetation element masks exceed aggregate decoded budget", {}, {},
                "asset.graphics.vegetation-scene"));
        auto image = AssetRef::parse(encodedAsset);
        if (!image) return Result<std::map<std::string, graphics::VegetationMask>>::failure(image.status());
        asset::EvpackImageDecodeLimits limits;
        limits.maximumDimension = 2048;
        limits.maximumPixels = 4 * 1024 * 1024;
        limits.maximumDecodedBytes = maximumDecodedBytes - usedBytes;
        auto decoded = asset::decodeEvpackImage(reader, image.value(), capabilities, limits);
        if (!decoded) return Result<std::map<std::string, graphics::VegetationMask>>::failure(decoded.status());
        const std::uint64_t pixelCount = std::uint64_t(decoded.value().width) * decoded.value().height;
        const std::uint64_t decodedBytes = decoded.value().pixels.size();
        const std::uint64_t maskBytes = pixelCount * sizeof(glm::vec4);
        if (decodedBytes > maximumDecodedBytes - usedBytes ||
            maskBytes > maximumDecodedBytes - usedBytes - decodedBytes)
            return Result<std::map<std::string, graphics::VegetationMask>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "vegetation element masks exceed aggregate decoded budget", {}, {},
                "asset.graphics.vegetation-scene"));
        graphics::VegetationMask mask;
        mask.width = decoded.value().width;
        mask.height = decoded.value().height;
        mask.pixels.resize(std::size_t(pixelCount));
        for (std::size_t index = 0; index < mask.pixels.size(); ++index) {
            const auto offset = index * 4;
            mask.pixels[index] = {decoded.value().pixels[offset] / 255.f, decoded.value().pixels[offset + 1] / 255.f,
                                  decoded.value().pixels[offset + 2] / 255.f,
                                  decoded.value().pixels[offset + 3] / 255.f};
        }
        usedBytes += decodedBytes + maskBytes;
        output.emplace(guid, std::move(mask));
    }
    return Result<std::map<std::string, graphics::VegetationMask>>::success(std::move(output));
    } catch (const std::bad_alloc&) {
        return Result<std::map<std::string, graphics::VegetationMask>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation element mask allocation failed", {}, {},
                              "asset.graphics.vegetation-scene"));
    }
}

Result<VegetationSceneElementPixel> evaluateVegetationSceneElementPixel(
    const VegetationSceneElement& element, const VegetationSceneElementPixelInput& input) {
    if (!finite(input.localUv) || !finite(input.mainSample) || !finite(input.elementParams) ||
        !finite(input.vertexColor) || !std::isfinite(input.worldPosition.x) || !std::isfinite(input.worldPosition.y) ||
        !std::isfinite(input.worldPosition.z) || !std::isfinite(input.worldNormal.x) || !std::isfinite(input.worldNormal.y) ||
        !std::isfinite(input.worldNormal.z) || !finite(input.velocityDirection) || !finite(input.noiseSample) ||
        !std::isfinite(input.terrainHeight) ||
        !unit(input.volumeFade) || !std::isfinite(input.season) || input.season < 0.f ||
        input.season > 4.f || !unit(element.intensity) || element.blendRgb < 0 || element.blendRgb > 2 ||
        element.blendAlpha < 0 || element.blendAlpha > 1)
        return Result<VegetationSceneElementPixel>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation element pixel inputs are invalid", {}, {},
                              "asset.graphics.vegetation-scene"));

    VegetationSceneElementPixel output;
    output.blendRgb = element.blendRgb;
    output.blendAlpha = element.blendAlpha;
    if (!element.enabled) return Result<VegetationSceneElementPixel>::success(output);

    const glm::vec3 remappedRgb(
        remap(input.mainSample.r, element.remap[0], element.remap[1], 0.f),
        remap(input.mainSample.g, element.remap[0], element.remap[1], 0.f),
        remap(input.mainSample.b, element.remap[0], element.remap[1], 0.f));
    const float remappedAlpha = remap(input.mainSample.a, element.remap[2], element.remap[3], .0001f);
    const glm::vec2 centered = input.localUv * 2.f - 1.f;
    const float radial = std::clamp(std::clamp(1.f - glm::length(centered), 0.f, 1.f), .0001f, .9999f);
    const float falloff = remap(radial, element.remap[4], element.remap[5], .0001f);
    const float volume = element.volumeFade ? input.volumeFade : 1.f;
    const float alpha = element.intensity * remappedAlpha * input.elementParams.a * input.vertexColor.a * falloff * volume;
    const glm::vec4 selected = seasonal(element, input.season);
    const float scalar = selected.x * remappedRgb.x * input.elementParams.x * input.vertexColor.r;
    const float alphaBlend = element.blendAlpha == 0 ? glm::mix(1.f, scalar, alpha) : scalar * alpha;

    if (element.kind == "color-effect") {
        output.value = {0.f, 0.f, 0.f, alphaBlend};
        output.colorMask = 1;
        output.rgbOperation = VegetationScenePixelBlend::Replace;
        output.alphaOperation = element.blendAlpha == 0 ? VegetationScenePixelBlend::Multiply
                                                        : VegetationScenePixelBlend::Add;
    } else if (element.kind == "color-map") {
        output.value = {selected.r * remappedRgb.r, selected.g * remappedRgb.g, selected.b * remappedRgb.b,
                        selected.a * alpha};
    } else if (element.kind == "color-noise") {
        const float minimum = propertyScalar(element, "_NoiseMinValue", 0.f);
        const float maximum = propertyScalar(element, "_NoiseMaxValue", 1.f);
        const float noise = maximum == minimum ? (input.noiseSample.r >= maximum ? 1.f : 0.f)
                                               : std::clamp((input.noiseSample.r - minimum) / (maximum - minimum), 0.f, 1.f);
        const glm::vec4 one = propertyVector(element, "_NoiseColorOne", glm::vec4(1.f));
        const glm::vec4 two = propertyVector(element, "_NoiseColorTwo", glm::vec4(1.f));
        const glm::vec4 color = glm::mix(one, two, noise);
        output.value = {color.r, color.g, color.b, color.a * alpha * selected.a};
    } else if (element.kind == "color-tint") {
        output.value = {selected.r * remappedRgb.r * input.elementParams.r * input.vertexColor.r,
                        selected.g * remappedRgb.g * input.elementParams.g * input.vertexColor.g,
                        selected.b * remappedRgb.b * input.elementParams.b * input.vertexColor.b,
                        selected.a * alpha};
        output.alphaOperation = VegetationScenePixelBlend::Add;
    } else if (element.kind == "extras-alpha") {
        output.value = {scalar, 0.f, 0.f, alphaBlend};
        output.colorMask = 1;
        output.rgbOperation = VegetationScenePixelBlend::Replace;
        output.alphaOperation = element.blendAlpha == 0 ? VegetationScenePixelBlend::Multiply
                                                        : VegetationScenePixelBlend::Add;
    } else if (element.kind == "extras-emissive") {
        output.value = {scalar, 0.f, 0.f, alpha};
        output.colorMask = 8;
    } else if (element.kind == "extras-overlay") {
        output.value = {0.f, 0.f, scalar, alpha};
        output.colorMask = 2;
    } else if (element.kind == "extras-wetness") {
        output.value = {0.f, scalar, 0.f, alpha};
        output.colorMask = 4;
    } else if (element.kind == "motion-wind-power") {
        output.value = {0.f, 0.f, scalar, alpha};
        output.colorMask = 2;
    } else if (element.kind == "motion-advanced") {
        const glm::vec3 forward3 = rotate(element.rotation, glm::vec3(0.f, 0.f, 1.f));
        const glm::vec3 texture3 = rotate(
            element.rotation, glm::vec3(remappedRgb.r * 2.f - 1.f, 0.f, remappedRgb.g * 2.f - 1.f));
        glm::vec2 direction;
        switch (element.directionMode) {
            case 10: direction = {forward3.x, forward3.z}; break;
            case 20: direction = {texture3.x, texture3.z}; break;
            case 30: direction = glm::vec2(input.vertexColor) * 2.f - 1.f; break;
            case 40: direction = input.velocityDirection; break;
            default:
                return Result<VegetationSceneElementPixel>::failure(
                    Diagnostic::error(DiagnosticCode::InvalidArgument, "TVE advanced motion direction mode is invalid",
                                      {}, {}, "asset.graphics.vegetation-scene"));
        }
        if (element.invertDirection) direction = -direction;
        const float noiseMinimum = propertyScalar(element, "_NoiseMinValue", 0.f);
        const float noiseMaximum = propertyScalar(element, "_NoiseMaxValue", 1.f);
        const float noiseDenominator = noiseMaximum - noiseMinimum;
        glm::vec2 noise = noiseDenominator == 0.f
                              ? glm::step(glm::vec2(noiseMaximum), glm::vec2(input.noiseSample))
                              : glm::clamp((glm::vec2(input.noiseSample) - noiseMinimum) / noiseDenominator,
                                           glm::vec2(0.f), glm::vec2(1.f));
        const float noiseIntensity = propertyScalar(element, "_NoiseIntensityValue", 0.f);
        const glm::vec2 encoded = glm::clamp(glm::mix(direction * .5f + .5f, noise, noiseIntensity),
                                             glm::vec2(0.f), glm::vec2(1.f));
        output.value = {encoded.x, encoded.y, element.motionPower,
                        element.intensity * remappedAlpha * input.elementParams.a * input.vertexColor.a * volume};
        output.colorMask = std::uint8_t(element.motionMode);
        output.alphaOperation = VegetationScenePixelBlend::Add;
    } else if (element.kind == "vertex-size") {
        output.value = {scalar, 0.f, 0.f, alphaBlend};
        output.colorMask = 1;
        output.rgbOperation = VegetationScenePixelBlend::Replace;
        output.alphaOperation = element.blendAlpha == 0 ? VegetationScenePixelBlend::Multiply
                                                        : VegetationScenePixelBlend::Add;
    } else if (element.kind == "motion-interaction") {
        const glm::vec3 localDirection(remappedRgb.r * 2.f - 1.f, 0.f, remappedRgb.g * 2.f - 1.f);
        glm::vec3 worldDirection = rotate(element.rotation, localDirection);
        if (element.invertDirection) worldDirection = -worldDirection;
        output.value = {std::clamp(worldDirection.x * .5f + .5f, 0.f, 1.f),
                        std::clamp(worldDirection.z * .5f + .5f, 0.f, 1.f), element.motionPower,
                        element.intensity * remappedAlpha * input.elementParams.a * input.vertexColor.a * volume};
        output.colorMask = std::uint8_t(element.motionMode);
        output.alphaOperation = VegetationScenePixelBlend::Add;
    } else if (element.kind == "vertex-conform-model") {
        output.value = {0.f, 0.f, input.worldPosition.y + propertyScalar(element, "_HeightOffsetValue", 0.f),
                        element.intensity * volume};
        output.colorMask = 2;
    } else if (element.kind == "vertex-conform-simple") {
        output.value = {0.f, 0.f, input.mainSample.r * propertyScalar(element, "_HeightValue", 1.f) +
                                         propertyScalar(element, "_HeightOffsetValue", 0.f),
                        element.intensity * volume};
        output.colorMask = 2;
    } else if (element.kind == "vertex-conform-terrain") {
        output.value = {0.f, 0.f, input.terrainHeight + propertyScalar(element, "_HeightOffsetValue", 0.f),
                        element.intensity * volume};
        output.colorMask = 2;
    } else if (element.kind == "vertex-height-offset") {
        output.value = {0.f, 0.f, propertyScalar(element, "_HeightOffsetValue", 0.f) * remappedAlpha *
                                         element.intensity * volume,
                        0.f};
        output.colorMask = 2;
        output.rgbOperation = VegetationScenePixelBlend::Add;
    } else if (element.kind == "vertex-height") {
        output.value = {0.f, 0.f, scalar * propertyScalar(element, "_HeightValue", 1.f) +
                                         propertyScalar(element, "_HeightOffsetValue", 0.f),
                        alpha};
        output.colorMask = 2;
    } else if (element.kind == "vertex-orientation-model") {
        const glm::vec3 normal = glm::length(input.worldNormal) > 0.f ? glm::normalize(input.worldNormal)
                                                                      : glm::vec3(0.f, 1.f, 0.f);
        output.value = {normal.x * .5f + .5f, normal.z * .5f + .5f, 0.f, element.intensity * volume};
        output.colorMask = 12;
    } else if (element.kind == "vertex-orientation-terrain") {
        output.value = {input.mainSample.r, input.mainSample.b, 0.f, element.intensity * volume};
        output.colorMask = 12;
    } else {
        return Result<VegetationSceneElementPixel>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "TVE Element pixel shader is not implemented", element.kind,
                              {}, "asset.graphics.vegetation-scene"));
    }
    if (!finite(output.value))
        return Result<VegetationSceneElementPixel>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation element pixel evaluation overflowed", {}, {},
                              "asset.graphics.vegetation-scene"));
    return Result<VegetationSceneElementPixel>::success(output);
}

glm::vec4 composeVegetationSceneElementPixel(glm::vec4 destination,
                                              const VegetationSceneElementPixel& source) noexcept {
    auto apply = [&](float dst, float src, VegetationScenePixelBlend operation) {
        switch (operation) {
            case VegetationScenePixelBlend::Alpha: return src * source.value.a + dst * (1.f - source.value.a);
            case VegetationScenePixelBlend::Multiply: return src * dst;
            case VegetationScenePixelBlend::Add: return src + dst;
            case VegetationScenePixelBlend::Replace: return src;
        }
        return dst;
    };
    glm::vec4 result = destination;
    if (source.colorMask & 8) result.r = apply(destination.r, source.value.r, source.rgbOperation);
    if (source.colorMask & 4) result.g = apply(destination.g, source.value.g, source.rgbOperation);
    if (source.colorMask & 2) result.b = apply(destination.b, source.value.b, source.rgbOperation);
    if (source.colorMask & 1) result.a = apply(destination.a, source.value.a, source.alphaOperation);
    return result;
}

Result<graphics::VegetationChannelAtlas> bakeVegetationSceneChannel(
    const LoadedVegetationScene& scene, const std::map<std::string, graphics::VegetationMask>& masks,
    graphics::VegetationChannel channel, const graphics::VegetationChannelAtlas& base, std::uint8_t layer,
    std::span<const glm::vec3> worldNormals, std::span<const float> terrainHeights,
    std::span<const glm::vec4> noiseSamples) {
    const std::uint64_t pixelCount64 = std::uint64_t(base.width) * base.height;
    if (base.width == 0 || base.height == 0 || base.width > 2048 || base.height > 2048 ||
        pixelCount64 > 4 * 1024 * 1024 || !std::isfinite(base.center.x) || !std::isfinite(base.center.y) ||
        !std::isfinite(base.center.z) || !std::isfinite(base.extent.x) || !std::isfinite(base.extent.y) ||
        !std::isfinite(base.extent.z) || base.extent.x <= 0.f || base.extent.y <= 0.f || base.extent.z <= 0.f ||
        layer > 8)
        return Result<graphics::VegetationChannelAtlas>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation scene channel geometry or layer is invalid",
                              {}, {}, "asset.graphics.vegetation-scene"));
    const std::size_t pixelCount = std::size_t(pixelCount64);
    if (base.pixels.size() != pixelCount ||
        (!worldNormals.empty() && worldNormals.size() != pixelCount) ||
        (!terrainHeights.empty() && terrainHeights.size() != pixelCount) ||
        (!noiseSamples.empty() && noiseSamples.size() != pixelCount))
        return Result<graphics::VegetationChannelAtlas>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation scene channel input sizes are inconsistent",
                              {}, {}, "asset.graphics.vegetation-scene"));
    try {
        graphics::VegetationChannelAtlas output = base;
        const std::size_t channelIndex = static_cast<std::size_t>(channel);
        const double stepX = 2.0 * base.extent.x / base.width;
        const double stepZ = 2.0 * base.extent.z / base.height;
        for (const auto& element : scene.elements) {
            if (!element.enabled || element.channel != channelIndex || !(element.layers & (1u << layer))) continue;
            const graphics::VegetationMask* mask = nullptr;
            if (!element.textureGuid.empty()) {
                const auto found = masks.find(element.textureGuid);
                if (found == masks.end())
                    return Result<graphics::VegetationChannelAtlas>::failure(
                        Diagnostic::error(DiagnosticCode::NotFound, "vegetation scene element mask is missing",
                                          element.textureGuid, {}, "asset.graphics.vegetation-scene"));
                mask = &found->second;
            }
            for (std::uint32_t y = 0; y < base.height; ++y) {
                for (std::uint32_t x = 0; x < base.width; ++x) {
                    const std::size_t index = std::size_t(y) * base.width + x;
                    const glm::vec3 world(float(double(base.center.x) - base.extent.x + (x + .5) * stepX),
                                          base.center.y,
                                          float(double(base.center.z) - base.extent.z + (y + .5) * stepZ));
                    const glm::vec3 delta = world - glm::vec3(element.position[0], element.position[1], element.position[2]);
                    const glm::vec3 local = inverseRotate(element.rotation, delta) /
                                            glm::vec3(element.scale[0], element.scale[1], element.scale[2]);
                    const glm::vec2 uv(local.x + .5f, local.z + .5f);
                    if (uv.x < 0.f || uv.x > 1.f || uv.y < 0.f || uv.y > 1.f) continue;
                    VegetationSceneElementPixelInput input;
                    input.localUv = uv;
                    const bool flipped = element.kind == "motion-interaction" || element.kind == "motion-advanced" ||
                                         element.kind == "vertex-conform-simple" ||
                                         element.kind == "vertex-orientation-terrain" ||
                                         element.kind == "vertex-height-offset";
                    input.mainSample = mask ? sampleMask(*mask, flipped ? glm::vec2(1.f) - uv : uv) : glm::vec4(1.f);
                    input.worldPosition = world;
                    if (!worldNormals.empty()) input.worldNormal = worldNormals[index];
                    if (!terrainHeights.empty()) input.terrainHeight = terrainHeights[index];
                    if (!noiseSamples.empty()) input.noiseSample = noiseSamples[index];
                    input.season = scene.control.season;
                    if (element.volumeFade) {
                        const glm::vec2 atlasUv((world.x - base.center.x) / (2.f * base.extent.x) + .5f,
                                                (world.z - base.center.z) / (2.f * base.extent.z) + .5f);
                        const glm::vec2 edge = glm::abs(atlasUv * 2.002f - 1.001f);
                        if (scene.volume.edgeFade >= 1.f)
                            input.volumeFade = edge.x <= 1.f && edge.y <= 1.f ? 1.f : 0.f;
                        else {
                            const glm::vec2 faded = glm::clamp((edge - scene.volume.edgeFade) /
                                                                  (1.f - scene.volume.edgeFade),
                                                              glm::vec2(0.f), glm::vec2(1.f));
                            input.volumeFade = 1.f - std::clamp(glm::dot(faded, faded), 0.f, 1.f);
                        }
                    }
                    auto source = evaluateVegetationSceneElementPixel(element, input);
                    if (!source) return Result<graphics::VegetationChannelAtlas>::failure(source.status());
                    output.pixels[index] = composeVegetationSceneElementPixel(output.pixels[index], source.value());
                }
            }
        }
        return Result<graphics::VegetationChannelAtlas>::success(std::move(output));
    } catch (const std::bad_alloc&) {
        return Result<graphics::VegetationChannelAtlas>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation scene channel allocation failed", {}, {},
                              "asset.graphics.vegetation-scene"));
    }
}

Result<graphics::VegetationAtlas> bakeVegetationSceneElements(
    const LoadedVegetationScene& scene, const std::map<std::string, graphics::VegetationMask>& masks,
    const graphics::VegetationAtlas& base, std::array<std::uint8_t, 4> layers,
    std::span<const glm::vec3> worldNormals, std::span<const float> terrainHeights,
    std::span<const glm::vec4> noiseSamples) {
    graphics::VegetationAtlas output;
    output.width = base.width;
    output.height = base.height;
    output.center = base.center;
    output.extent = base.extent;
    for (std::size_t index = 0; index < 4; ++index) {
        graphics::VegetationChannelAtlas channelBase{base.width, base.height, base.center, base.extent,
                                                      base.channels[index]};
        auto baked = bakeVegetationSceneChannel(scene, masks, static_cast<graphics::VegetationChannel>(index),
                                                channelBase, layers[index], worldNormals, terrainHeights, noiseSamples);
        if (!baked) return Result<graphics::VegetationAtlas>::failure(baked.status());
        output.channels[index] = std::move(baked).takeValue().pixels;
    }
    return Result<graphics::VegetationAtlas>::success(std::move(output));
}

Result<graphics::VegetationChannelAtlas> convertVegetationSceneChannelToNative(
    graphics::VegetationChannel channel, const graphics::VegetationChannelAtlas& tveAtlas) {
    const std::uint64_t pixelCount64 = std::uint64_t(tveAtlas.width) * tveAtlas.height;
    if (tveAtlas.width == 0 || tveAtlas.height == 0 || tveAtlas.width > 2048 || tveAtlas.height > 2048 ||
        pixelCount64 > 4 * 1024 * 1024 || !std::isfinite(tveAtlas.center.x) || !std::isfinite(tveAtlas.center.y) ||
        !std::isfinite(tveAtlas.center.z) || !std::isfinite(tveAtlas.extent.x) ||
        !std::isfinite(tveAtlas.extent.y) || !std::isfinite(tveAtlas.extent.z) || tveAtlas.extent.x <= 0.f ||
        tveAtlas.extent.y <= 0.f || tveAtlas.extent.z <= 0.f)
        return Result<graphics::VegetationChannelAtlas>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "TVE vegetation channel geometry is invalid", {}, {},
                              "asset.graphics.vegetation-scene"));
    const std::size_t pixelCount = std::size_t(pixelCount64);
    if (tveAtlas.pixels.size() != pixelCount ||
        std::any_of(tveAtlas.pixels.begin(), tveAtlas.pixels.end(), [](glm::vec4 value) { return !finite(value); }))
        return Result<graphics::VegetationChannelAtlas>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "TVE vegetation channel pixels are invalid", {}, {},
                              "asset.graphics.vegetation-scene"));
    try {
        graphics::VegetationChannelAtlas native = tveAtlas;
        if (channel == graphics::VegetationChannel::Motion) {
            for (auto& motion : native.pixels) {
                motion.x = motion.x * 2.f - 1.f;
                motion.y = motion.y * 2.f - 1.f;
            }
        }
        return Result<graphics::VegetationChannelAtlas>::success(std::move(native));
    } catch (const std::bad_alloc&) {
        return Result<graphics::VegetationChannelAtlas>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "native vegetation channel allocation failed", {}, {},
                              "asset.graphics.vegetation-scene"));
    }
}

Result<graphics::VegetationAtlas> convertVegetationSceneAtlasToNative(
    const graphics::VegetationAtlas& tveAtlas) {
    graphics::VegetationAtlas native;
    native.width = tveAtlas.width;
    native.height = tveAtlas.height;
    native.center = tveAtlas.center;
    native.extent = tveAtlas.extent;
    for (std::size_t index = 0; index < 4; ++index) {
        graphics::VegetationChannelAtlas source{tveAtlas.width, tveAtlas.height, tveAtlas.center, tveAtlas.extent,
                                                tveAtlas.channels[index]};
        auto converted = convertVegetationSceneChannelToNative(static_cast<graphics::VegetationChannel>(index), source);
        if (!converted) return Result<graphics::VegetationAtlas>::failure(converted.status());
        native.channels[index] = std::move(converted).takeValue().pixels;
    }
    return Result<graphics::VegetationAtlas>::success(std::move(native));
}

Result<std::unique_ptr<VegetationSceneGpuRuntime>> VegetationSceneGpuRuntime::create(
    graphics::IResourceFactory& factory, const graphics::PbrSurface& baseSurface,
    const graphics::VegetationMotion& baseMotion, const LoadedVegetationScene& scene,
    const std::map<std::string, graphics::VegetationMask>& masks, const VegetationSceneGpuBuild& build) {
    if (build.sourceRevision == 0)
        return Result<std::unique_ptr<VegetationSceneGpuRuntime>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation scene GPU revision must be nonzero", {}, {},
                              "asset.graphics.vegetation-scene"));
    auto projected = projectVegetationScene(baseSurface, baseMotion, scene);
    if (!projected) return Result<std::unique_ptr<VegetationSceneGpuRuntime>>::failure(projected.status());
    try {
        std::array<std::array<graphics::VegetationChannelAtlas, 9>, 4> nativeChannels;
        for (std::size_t channel = 0; channel < nativeChannels.size(); ++channel) {
            const auto& input = build.channels[channel];
            for (std::uint8_t layer = 0; layer < nativeChannels[channel].size(); ++layer) {
                auto tve = bakeVegetationSceneChannel(scene, masks,
                                                       static_cast<graphics::VegetationChannel>(channel),
                                                       input.baseLayers[layer], layer, input.worldNormals,
                                                       input.terrainHeights, input.noiseSamples);
                if (!tve) return Result<std::unique_ptr<VegetationSceneGpuRuntime>>::failure(tve.status());
                auto native = convertVegetationSceneChannelToNative(
                    static_cast<graphics::VegetationChannel>(channel), tve.value());
                if (!native) return Result<std::unique_ptr<VegetationSceneGpuRuntime>>::failure(native.status());
                nativeChannels[channel][layer] = std::move(native).takeValue();
            }
        }

        auto runtime = std::unique_ptr<VegetationSceneGpuRuntime>(new VegetationSceneGpuRuntime(factory));
        runtime->projection_ = std::move(projected).takeValue();
        runtime->sourceRevision_ = build.sourceRevision;
        auto uploaded = graphics::uploadVegetationGpuFieldSet(factory, nativeChannels[0], nativeChannels[1],
                                                              nativeChannels[2], nativeChannels[3],
                                                              build.sourceRevision);
        if (!uploaded) return Result<std::unique_ptr<VegetationSceneGpuRuntime>>::failure(uploaded.status());
        runtime->fields_ = std::move(uploaded).takeValue();

        graphics::VegetationGpuRuntime gpu = build.runtime;
        const auto& motion = runtime->projection_.surface.vegetationMotion;
        gpu.motionDirection = motion.globalDirection;
        gpu.worldOrigin = motion.worldOrigin;
        gpu.time = runtime->projection_.motion.time;
        gpu.globalBending = motion.globalBending;
        gpu.globalBranch = motion.globalBranch;
        gpu.globalFlutter = motion.globalFlutter;
        gpu.noiseTiling = motion.noiseTiling;
        gpu.motionFadeDistance = motion.fadeDistance;
        gpu.cameraFadeMin = runtime->projection_.surface.vegetationAlpha.cameraFadeMin;
        gpu.cameraFadeMax = runtime->projection_.surface.vegetationAlpha.cameraFadeMax;
        gpu.fadeNoiseTiling = runtime->projection_.surface.vegetationAlpha.noiseTiling;
        auto bound = graphics::bindVegetationGpuFields(runtime->projection_.surface, runtime->fields_,
                                                       build.sourceRevision, gpu);
        if (!bound) {
            auto released = runtime->release();
            if (!released)
                return Result<std::unique_ptr<VegetationSceneGpuRuntime>>::failure(
                    Diagnostic::error(DiagnosticCode::Failed, "vegetation scene GPU binding rollback failed", {}, {},
                                      "asset.graphics.vegetation-scene"));
            return Result<std::unique_ptr<VegetationSceneGpuRuntime>>::failure(bound.status());
        }
        return Result<std::unique_ptr<VegetationSceneGpuRuntime>>::success(std::move(runtime));
    } catch (const std::bad_alloc&) {
        return Result<std::unique_ptr<VegetationSceneGpuRuntime>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation scene GPU runtime allocation failed", {}, {},
                              "asset.graphics.vegetation-scene"));
    }
}

Result<VegetationSceneGpuPublication> VegetationSceneGpuRuntime::replace(
    std::uint64_t expectedRevision, const graphics::PbrSurface& baseSurface,
    const graphics::VegetationMotion& baseMotion, const LoadedVegetationScene& scene,
    const std::map<std::string, graphics::VegetationMask>& masks, const VegetationSceneGpuBuild& build) {
    if (!factory_)
        return Result<VegetationSceneGpuPublication>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "released vegetation scene runtime cannot be replaced", {}, {},
                              "asset.graphics.vegetation-scene"));
    if (expectedRevision != sourceRevision_)
        return Result<VegetationSceneGpuPublication>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "vegetation scene GPU revision changed before replacement", {},
                              {}, "asset.graphics.vegetation-scene"));
    if (build.sourceRevision <= sourceRevision_)
        return Result<VegetationSceneGpuPublication>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "vegetation scene replacement revision must increase",
                              {}, {}, "asset.graphics.vegetation-scene"));

    auto candidate = create(*factory_, baseSurface, baseMotion, scene, masks, build);
    if (!candidate) return Result<VegetationSceneGpuPublication>::failure(candidate.status());
    try {
        retiredFields_.reserve(retiredFields_.size() + 1);
        retiredFields_.push_back(std::move(fields_));
    } catch (const std::bad_alloc&) {
        return Result<VegetationSceneGpuPublication>::failure(
            Diagnostic::error(DiagnosticCode::Failed, "vegetation scene replacement retirement allocation failed", {},
                              {}, "asset.graphics.vegetation-scene"));
    }

    fields_ = std::move(candidate.value()->fields_);
    std::swap(projection_, candidate.value()->projection_);
    sourceRevision_ = build.sourceRevision;
    candidate.value()->factory_ = nullptr;

    for (auto current = retiredFields_.begin(); current != retiredFields_.end();) {
        auto released = graphics::releaseVegetationGpuFieldSet(*factory_, *current);
        if (released)
            current = retiredFields_.erase(current);
        else
            ++current;
    }
    return Result<VegetationSceneGpuPublication>::success({sourceRevision_, retiredFields_.size()});
}

VegetationSceneGpuRuntime::~VegetationSceneGpuRuntime() {
    auto released = release();
    if (!released)
        std::fprintf(stderr, "vegetation scene GPU release failed: %s\n", released.error()->message().c_str());
}

Result<void> VegetationSceneGpuRuntime::release() {
    if (!factory_) return Result<void>::success();
    projection_.surface.vegetationExtras.texture = nullptr;
    projection_.surface.vegetationColors.texture = nullptr;
    projection_.surface.vegetationMotion.texture = nullptr;
    projection_.surface.vegetationVertex.texture = nullptr;
    auto released = graphics::releaseVegetationGpuFieldSet(*factory_, fields_);
    Status firstFailure;
    if (!released) firstFailure = released.status();
    for (auto current = retiredFields_.begin(); current != retiredFields_.end();) {
        auto retired = graphics::releaseVegetationGpuFieldSet(*factory_, *current);
        if (retired)
            current = retiredFields_.erase(current);
        else {
            if (firstFailure.isSuccess()) firstFailure = retired.status();
            ++current;
        }
    }
    if (firstFailure.isFailure()) return Result<void>::failure(firstFailure);
    factory_ = nullptr;
    return Result<void>::success();
}
}  // namespace eve::asset_graphics
