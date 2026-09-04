#include "stylize/MeshVfxAsset.h"

#include "common/Value.h"
#include "common/Exception.h"

#include <cmath>
#include <algorithm>
#include <limits>
#include <set>

namespace eve::stylize {
namespace {

template <typename T>
eve::Result<T> failure(eve::DiagnosticCode code, std::string message, std::string path = {}) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message), std::move(path)));
}

bool finiteNonNegative(float value) { return std::isfinite(value) && value >= 0.f; }

eve::Result<void> rejectUnknown(const eve::Value::Object& object, const std::set<std::string>& allowed,
                                const std::string& path) {
    for (const auto& [key, value] : object) {
        (void)value;
        if (!allowed.contains(key))
            return failure<void>(eve::DiagnosticCode::InvalidArgument, "unknown mesh VFX field", path + "." + key);
    }
    return eve::Result<void>::success();
}

eve::Result<float> number(const eve::Value& value, const std::string& path) {
    if (const auto* v = value.getIf<double>()) return eve::Result<float>::success(static_cast<float>(*v));
    if (const auto* v = value.getIf<std::int64_t>()) return eve::Result<float>::success(static_cast<float>(*v));
    return failure<float>(eve::DiagnosticCode::InvalidArgument, "expected number", path);
}

eve::Result<MeshEffectPlayback> parsePlayback(const eve::Value* value, const std::string& path) {
    MeshEffectPlayback result{};
    if (!value) return eve::Result<MeshEffectPlayback>::success(result);
    const auto* object = value->getIf<eve::Value::Object>();
    if (!object) return failure<MeshEffectPlayback>(eve::DiagnosticCode::InvalidArgument, "expected object", path);
    auto known = rejectUnknown(*object, {"fadeIn", "hold", "fadeOut", "loop"}, path);
    if (!known) return eve::Result<MeshEffectPlayback>::failure(known.status());
    const auto read = [&](const char* key, float& target) -> eve::Result<void> {
        const auto it = object->find(key);
        if (it == object->end()) return eve::Result<void>::success();
        auto parsed = number(it->second, path + "." + key);
        if (!parsed) return eve::Result<void>::failure(parsed.status());
        target = std::move(parsed).takeValue();
        return eve::Result<void>::success();
    };
    auto status = read("fadeIn", result.fadeIn);
    if (!status) return eve::Result<MeshEffectPlayback>::failure(status.status());
    status = read("hold", result.duration);
    if (!status) return eve::Result<MeshEffectPlayback>::failure(status.status());
    status = read("fadeOut", result.fadeOut);
    if (!status) return eve::Result<MeshEffectPlayback>::failure(status.status());
    const auto loop = object->find("loop");
    if (loop != object->end()) {
        const auto* enabled = loop->second.getIf<bool>();
        if (!enabled) return failure<MeshEffectPlayback>(eve::DiagnosticCode::InvalidArgument, "expected boolean", path + ".loop");
        result.loop = *enabled;
    }
    return eve::Result<MeshEffectPlayback>::success(result);
}

eve::Result<MeshVfxLayerAsset> parseLayer(const eve::Value& value, std::size_t index) {
    const std::string path = "layers[" + std::to_string(index) + "]";
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object) return failure<MeshVfxLayerAsset>(eve::DiagnosticCode::InvalidArgument, "expected object", path);
    auto known = rejectUnknown(*object, {"style", "playback", "parameters", "curves"}, path);
    if (!known) return eve::Result<MeshVfxLayerAsset>::failure(known.status());
    const auto style = object->find("style");
    if (style == object->end() || !style->second.isString())
        return failure<MeshVfxLayerAsset>(eve::DiagnosticCode::InvalidArgument, "style must be a string", path + ".style");
    MeshVfxLayerAsset layer;
    layer.style = style->second.asString();
    const auto playback = object->find("playback");
    auto parsedPlayback = parsePlayback(playback == object->end() ? nullptr : &playback->second, path + ".playback");
    if (!parsedPlayback) return eve::Result<MeshVfxLayerAsset>::failure(parsedPlayback.status());
    layer.playback = std::move(parsedPlayback).takeValue();
    const auto parameters = object->find("parameters");
    if (parameters != object->end()) {
        const auto* values = parameters->second.getIf<eve::Value::Object>();
        if (!values) return failure<MeshVfxLayerAsset>(eve::DiagnosticCode::InvalidArgument, "expected object", path + ".parameters");
        for (const auto& [name, raw] : *values) {
            auto parsed = number(raw, path + ".parameters." + name);
            if (!parsed) return eve::Result<MeshVfxLayerAsset>::failure(parsed.status());
            layer.floatParameters.emplace(name, std::move(parsed).takeValue());
        }
    }
    const auto curves = object->find("curves");
    if (curves != object->end()) {
        const auto* values = curves->second.getIf<eve::Value::Object>();
        if (!values) return failure<MeshVfxLayerAsset>(eve::DiagnosticCode::InvalidArgument, "expected object", path + ".curves");
        for (const auto& [name, rawCurve] : *values) {
            const auto* rawKeys = rawCurve.getIf<eve::Value::Array>();
            if (!rawKeys) return failure<MeshVfxLayerAsset>(eve::DiagnosticCode::InvalidArgument, "curve must be an array", path + ".curves." + name);
            MeshVfxFloatCurve curve;
            for (std::size_t keyIndex = 0; keyIndex < rawKeys->size(); ++keyIndex) {
                const auto* pair = (*rawKeys)[keyIndex].getIf<eve::Value::Array>();
                const std::string keyPath = path + ".curves." + name + "[" + std::to_string(keyIndex) + "]";
                if (!pair || pair->size() != 2)
                    return failure<MeshVfxLayerAsset>(eve::DiagnosticCode::InvalidArgument, "curve key must be [time, value]", keyPath);
                auto time = number((*pair)[0], keyPath + "[0]");
                if (!time) return eve::Result<MeshVfxLayerAsset>::failure(time.status());
                auto keyValue = number((*pair)[1], keyPath + "[1]");
                if (!keyValue) return eve::Result<MeshVfxLayerAsset>::failure(keyValue.status());
                curve.keys.push_back({std::move(time).takeValue(), std::move(keyValue).takeValue()});
            }
            layer.floatCurves.emplace(name, std::move(curve));
        }
    }
    return eve::Result<MeshVfxLayerAsset>::success(std::move(layer));
}

eve::Result<TrailSettings> parseTrail(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object) return failure<TrailSettings>(eve::DiagnosticCode::InvalidArgument, "expected object", "trail");
    auto known = rejectUnknown(*object, {"maxSamples", "lifetime", "minDistance", "teleportDistance",
                                         "rootAttachment", "tipAttachment", "rootOffset", "tipOffset"}, "trail");
    if (!known) return eve::Result<TrailSettings>::failure(known.status());
    TrailSettings trail{};
    const auto read = [&](const char* key, float& target) -> eve::Result<void> {
        const auto it = object->find(key);
        if (it == object->end()) return eve::Result<void>::success();
        auto parsed = number(it->second, std::string("trail.") + key);
        if (!parsed) return eve::Result<void>::failure(parsed.status());
        target = std::move(parsed).takeValue();
        return eve::Result<void>::success();
    };
    const auto maxSamples = object->find("maxSamples");
    if (maxSamples != object->end()) {
        const auto* count = maxSamples->second.getIf<std::int64_t>();
        if (!count || *count <= 0) return failure<TrailSettings>(eve::DiagnosticCode::InvalidArgument, "maxSamples must be a positive integer", "trail.maxSamples");
        trail.maxSamples = static_cast<std::size_t>(*count);
    }
    auto status = read("lifetime", trail.lifetime);
    if (!status) return eve::Result<TrailSettings>::failure(status.status());
    status = read("minDistance", trail.minSampleDistance);
    if (!status) return eve::Result<TrailSettings>::failure(status.status());
    status = read("teleportDistance", trail.teleportDistance);
    if (!status) return eve::Result<TrailSettings>::failure(status.status());
    return eve::Result<TrailSettings>::success(trail);
}

eve::Result<eve::AttachmentPoint> parseAttachmentOffset(const eve::Value* value, const std::string& path) {
    if (!value) return eve::Result<eve::AttachmentPoint>::success({});
    const auto* array = value->getIf<eve::Value::Array>();
    if (!array || array->size() != 3)
        return failure<eve::AttachmentPoint>(eve::DiagnosticCode::InvalidArgument,
                                              "attachment offset must be a three-number array", path);
    eve::AttachmentPoint point;
    float* components[] = {&point.x, &point.y, &point.z};
    for (std::size_t index = 0; index < 3; ++index) {
        auto parsed = number((*array)[index], path + "[" + std::to_string(index) + "]");
        if (!parsed) return eve::Result<eve::AttachmentPoint>::failure(parsed.status());
        *components[index] = std::move(parsed).takeValue();
    }
    return eve::Result<eve::AttachmentPoint>::success(point);
}

eve::Result<std::optional<MeshVfxTrailBinding>> parseTrailBinding(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object) return failure<std::optional<MeshVfxTrailBinding>>(eve::DiagnosticCode::InvalidArgument,
                                                                    "expected object", "trail");
    const auto root = object->find("rootAttachment");
    const auto tip = object->find("tipAttachment");
    if (root == object->end() && tip == object->end())
        return eve::Result<std::optional<MeshVfxTrailBinding>>::success(std::nullopt);
    if (root == object->end() || tip == object->end() || !root->second.isString() || !tip->second.isString())
        return failure<std::optional<MeshVfxTrailBinding>>(eve::DiagnosticCode::InvalidArgument,
                                                            "trail rootAttachment and tipAttachment must both be strings", "trail");
    const auto rootOffsetIt = object->find("rootOffset");
    const auto tipOffsetIt = object->find("tipOffset");
    auto rootOffset = parseAttachmentOffset(rootOffsetIt == object->end() ? nullptr : &rootOffsetIt->second,
                                            "trail.rootOffset");
    if (!rootOffset) return eve::Result<std::optional<MeshVfxTrailBinding>>::failure(rootOffset.status());
    auto tipOffset = parseAttachmentOffset(tipOffsetIt == object->end() ? nullptr : &tipOffsetIt->second,
                                           "trail.tipOffset");
    if (!tipOffset) return eve::Result<std::optional<MeshVfxTrailBinding>>::failure(tipOffset.status());
    return eve::Result<std::optional<MeshVfxTrailBinding>>::success(MeshVfxTrailBinding{
        root->second.asString(), tip->second.asString(), std::move(rootOffset).takeValue(),
        std::move(tipOffset).takeValue()});
}

eve::Result<std::vector<MeshVfxEventAsset>> parseEvents(const eve::Value& value) {
    const auto* array = value.getIf<eve::Value::Array>();
    if (!array) return failure<std::vector<MeshVfxEventAsset>>(eve::DiagnosticCode::InvalidArgument, "events must be an array", "events");
    std::vector<MeshVfxEventAsset> events;
    events.reserve(array->size());
    for (std::size_t index = 0; index < array->size(); ++index) {
        const std::string path = "events[" + std::to_string(index) + "]";
        const auto* object = (*array)[index].getIf<eve::Value::Object>();
        if (!object) return failure<std::vector<MeshVfxEventAsset>>(eve::DiagnosticCode::InvalidArgument, "event must be an object", path);
        auto known = rejectUnknown(*object, {"time", "name"}, path);
        if (!known) return eve::Result<std::vector<MeshVfxEventAsset>>::failure(known.status());
        const auto timeIt = object->find("time");
        const auto nameIt = object->find("name");
        if (timeIt == object->end()) return failure<std::vector<MeshVfxEventAsset>>(eve::DiagnosticCode::InvalidArgument, "event time is required", path + ".time");
        if (nameIt == object->end() || !nameIt->second.isString())
            return failure<std::vector<MeshVfxEventAsset>>(eve::DiagnosticCode::InvalidArgument, "event name must be a string", path + ".name");
        auto time = number(timeIt->second, path + ".time");
        if (!time) return eve::Result<std::vector<MeshVfxEventAsset>>::failure(time.status());
        events.push_back({std::move(time).takeValue(), nameIt->second.asString()});
    }
    return eve::Result<std::vector<MeshVfxEventAsset>>::success(std::move(events));
}

eve::Result<std::vector<std::string>> parseEventNames(const eve::Value* value, const std::string& path) {
    if (!value) return eve::Result<std::vector<std::string>>::success({});
    const auto* array = value->getIf<eve::Value::Array>();
    if (!array) return failure<std::vector<std::string>>(eve::DiagnosticCode::InvalidArgument,
                                                         "animation trigger list must be an array", path);
    std::vector<std::string> names;
    names.reserve(array->size());
    for (std::size_t index = 0; index < array->size(); ++index) {
        if (!(*array)[index].isString())
            return failure<std::vector<std::string>>(eve::DiagnosticCode::InvalidArgument,
                                                      "animation trigger name must be a string",
                                                      path + "[" + std::to_string(index) + "]");
        names.push_back((*array)[index].asString());
    }
    return eve::Result<std::vector<std::string>>::success(std::move(names));
}

eve::Result<MeshVfxAnimationTriggers> parseAnimationTriggers(const eve::Value& value) {
    const auto* object = value.getIf<eve::Value::Object>();
    if (!object) return failure<MeshVfxAnimationTriggers>(eve::DiagnosticCode::InvalidArgument,
                                                           "animationTriggers must be an object", "animationTriggers");
    auto known = rejectUnknown(*object, {"play", "stop", "trailBreak"}, "animationTriggers");
    if (!known) return eve::Result<MeshVfxAnimationTriggers>::failure(known.status());
    MeshVfxAnimationTriggers triggers;
    for (const auto& [key, destination] : std::initializer_list<std::pair<const char*, std::vector<std::string>*>>{
             {"play", &triggers.play}, {"stop", &triggers.stop}, {"trailBreak", &triggers.trailBreak}}) {
        const auto found = object->find(key);
        auto parsed = parseEventNames(found == object->end() ? nullptr : &found->second,
                                      std::string("animationTriggers.") + key);
        if (!parsed) return eve::Result<MeshVfxAnimationTriggers>::failure(parsed.status());
        *destination = std::move(parsed).takeValue();
    }
    return eve::Result<MeshVfxAnimationTriggers>::success(std::move(triggers));
}

}  // namespace

eve::Result<MeshVfxAsset> MeshVfxAsset::fromJson(std::string_view json) {
    auto parsed = eve::Value::fromJson(json);
    if (!parsed) return eve::Result<MeshVfxAsset>::failure(parsed.status());
    const auto* root = parsed.value().getIf<eve::Value::Object>();
    if (!root) return failure<MeshVfxAsset>(eve::DiagnosticCode::InvalidArgument, "mesh VFX root must be an object");
    const auto versionIt = root->find("schemaVersion");
    if (versionIt == root->end() || !versionIt->second.getIf<std::int64_t>())
        return failure<MeshVfxAsset>(eve::DiagnosticCode::InvalidArgument, "schemaVersion must be an integer", "schemaVersion");
    const auto version = *versionIt->second.getIf<std::int64_t>();
    MeshVfxAsset asset;
    if (version == 0) {
        auto known = rejectUnknown(*root, {"schemaVersion", "style", "playback", "parameters", "curves", "trail", "events", "animationTriggers"}, "$root");
        if (!known) return eve::Result<MeshVfxAsset>::failure(known.status());
        eve::Value::Object layerObject;
        for (const char* key : {"style", "playback", "parameters", "curves"}) {
            const auto it = root->find(key);
            if (it != root->end()) layerObject.emplace(key, it->second);
        }
        auto layer = parseLayer(eve::Value(std::move(layerObject)), 0);
        if (!layer) return eve::Result<MeshVfxAsset>::failure(layer.status());
        asset.layers.push_back(std::move(layer).takeValue());
    } else if (version == schemaVersion) {
        auto known = rejectUnknown(*root, {"schema", "schemaVersion", "layers", "trail", "events", "animationTriggers"}, "$root");
        if (!known) return eve::Result<MeshVfxAsset>::failure(known.status());
        const auto schema = root->find("schema");
        if (schema == root->end() || !schema->second.isString() || schema->second.asString() != schemaId)
            return failure<MeshVfxAsset>(eve::DiagnosticCode::InvalidArgument, "unsupported mesh VFX schema", "schema");
        const auto layers = root->find("layers");
        const auto* array = layers == root->end() ? nullptr : layers->second.getIf<eve::Value::Array>();
        if (!array) return failure<MeshVfxAsset>(eve::DiagnosticCode::InvalidArgument, "layers must be an array", "layers");
        for (std::size_t index = 0; index < array->size(); ++index) {
            auto layer = parseLayer((*array)[index], index);
            if (!layer) return eve::Result<MeshVfxAsset>::failure(layer.status());
            asset.layers.push_back(std::move(layer).takeValue());
        }
    } else {
        return failure<MeshVfxAsset>(eve::DiagnosticCode::UnknownVersion, "unsupported mesh VFX schema version", "schemaVersion");
    }
    const auto trail = root->find("trail");
    if (trail != root->end()) {
        auto parsedTrail = parseTrail(trail->second);
        if (!parsedTrail) return eve::Result<MeshVfxAsset>::failure(parsedTrail.status());
        asset.trail = std::move(parsedTrail).takeValue();
        auto binding = parseTrailBinding(trail->second);
        if (!binding) return eve::Result<MeshVfxAsset>::failure(binding.status());
        asset.trailBinding = std::move(binding).takeValue();
    }
    const auto events = root->find("events");
    if (events != root->end()) {
        auto parsedEvents = parseEvents(events->second);
        if (!parsedEvents) return eve::Result<MeshVfxAsset>::failure(parsedEvents.status());
        asset.events = std::move(parsedEvents).takeValue();
    }
    const auto animationTriggers = root->find("animationTriggers");
    if (animationTriggers != root->end()) {
        auto parsedTriggers = parseAnimationTriggers(animationTriggers->second);
        if (!parsedTriggers) return eve::Result<MeshVfxAsset>::failure(parsedTriggers.status());
        asset.animationTriggers = std::move(parsedTriggers).takeValue();
    }
    auto valid = asset.validate();
    if (!valid) return eve::Result<MeshVfxAsset>::failure(valid.status());
    return eve::Result<MeshVfxAsset>::success(std::move(asset));
}

eve::Result<std::string> MeshVfxAsset::toJson() const {
    auto valid = validate();
    if (!valid) return eve::Result<std::string>::failure(valid.status());
    eve::Value::Object root;
    root.emplace("schema", eve::Value(std::string(schemaId)));
    root.emplace("schemaVersion", eve::Value(static_cast<std::int64_t>(schemaVersion)));
    eve::Value::Array layerValues;
    layerValues.reserve(layers.size());
    for (const auto& layer : layers) {
        eve::Value::Object object;
        object.emplace("style", eve::Value(layer.style));
        eve::Value::Object playback;
        playback.emplace("fadeIn", eve::Value(static_cast<double>(layer.playback.fadeIn)));
        playback.emplace("hold", eve::Value(static_cast<double>(layer.playback.duration)));
        playback.emplace("fadeOut", eve::Value(static_cast<double>(layer.playback.fadeOut)));
        playback.emplace("loop", eve::Value(layer.playback.loop));
        object.emplace("playback", eve::Value(std::move(playback)));
        if (!layer.floatParameters.empty()) {
            eve::Value::Object parameters;
            for (const auto& [name, value] : layer.floatParameters)
                parameters.emplace(name, eve::Value(static_cast<double>(value)));
            object.emplace("parameters", eve::Value(std::move(parameters)));
        }
        if (!layer.floatCurves.empty()) {
            eve::Value::Object curves;
            for (const auto& [name, curve] : layer.floatCurves) {
                eve::Value::Array keys;
                for (const auto& key : curve.keys) {
                    eve::Value::Array pair;
                    pair.emplace_back(static_cast<double>(key.time));
                    pair.emplace_back(static_cast<double>(key.value));
                    keys.emplace_back(std::move(pair));
                }
                curves.emplace(name, eve::Value(std::move(keys)));
            }
            object.emplace("curves", eve::Value(std::move(curves)));
        }
        layerValues.emplace_back(std::move(object));
    }
    root.emplace("layers", eve::Value(std::move(layerValues)));
    if (trail) {
        eve::Value::Object value;
        value.emplace("maxSamples", eve::Value(static_cast<std::int64_t>(trail->maxSamples)));
        value.emplace("lifetime", eve::Value(static_cast<double>(trail->lifetime)));
        value.emplace("minDistance", eve::Value(static_cast<double>(trail->minSampleDistance)));
        value.emplace("teleportDistance", eve::Value(static_cast<double>(trail->teleportDistance)));
        if (trailBinding) {
            value.emplace("rootAttachment", eve::Value(trailBinding->rootAttachment));
            value.emplace("tipAttachment", eve::Value(trailBinding->tipAttachment));
            value.emplace("rootOffset", eve::Value::Array{static_cast<double>(trailBinding->rootOffset.x),
                                                           static_cast<double>(trailBinding->rootOffset.y),
                                                           static_cast<double>(trailBinding->rootOffset.z)});
            value.emplace("tipOffset", eve::Value::Array{static_cast<double>(trailBinding->tipOffset.x),
                                                          static_cast<double>(trailBinding->tipOffset.y),
                                                          static_cast<double>(trailBinding->tipOffset.z)});
        }
        root.emplace("trail", eve::Value(std::move(value)));
    }
    if (!events.empty()) {
        eve::Value::Array values;
        for (const auto& event : events) {
            eve::Value::Object value;
            value.emplace("time", eve::Value(static_cast<double>(event.time)));
            value.emplace("name", eve::Value(event.name));
            values.emplace_back(std::move(value));
        }
        root.emplace("events", eve::Value(std::move(values)));
    }
    if (!animationTriggers.play.empty() || !animationTriggers.stop.empty() || !animationTriggers.trailBreak.empty()) {
        eve::Value::Object value;
        const auto append = [&](const char* key, const std::vector<std::string>& names) {
            if (names.empty()) return;
            eve::Value::Array array;
            for (const auto& name : names) array.emplace_back(name);
            value.emplace(key, eve::Value(std::move(array)));
        };
        append("play", animationTriggers.play);
        append("stop", animationTriggers.stop);
        append("trailBreak", animationTriggers.trailBreak);
        root.emplace("animationTriggers", eve::Value(std::move(value)));
    }
    return eve::Value(std::move(root)).toJson();
}

eve::Result<void> MeshVfxAsset::validate() const {
    if (layers.empty()) return failure<void>(eve::DiagnosticCode::InvalidArgument, "mesh VFX requires at least one layer", "layers");
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const auto& layer = layers[index];
        const std::string path = "layers[" + std::to_string(index) + "]";
        if (layer.style.empty()) return failure<void>(eve::DiagnosticCode::InvalidArgument, "style must not be empty", path + ".style");
        if (!finiteNonNegative(layer.playback.fadeIn) || !finiteNonNegative(layer.playback.duration) ||
            !finiteNonNegative(layer.playback.fadeOut))
            return failure<void>(eve::DiagnosticCode::InvalidArgument, "playback durations must be finite and non-negative", path + ".playback");
        for (const auto& [name, value] : layer.floatParameters)
            if (name.empty() || !std::isfinite(value))
                return failure<void>(eve::DiagnosticCode::InvalidArgument, "parameter names must be non-empty and values finite", path + ".parameters");
        for (const auto& [name, curve] : layer.floatCurves) {
            if (name.empty() || curve.keys.empty())
                return failure<void>(eve::DiagnosticCode::InvalidArgument, "curve names and key arrays must not be empty", path + ".curves");
            float previous = -1.f;
            for (const auto& key : curve.keys) {
                if (!std::isfinite(key.time) || !std::isfinite(key.value) || key.time < 0.f || key.time > 1.f || key.time <= previous)
                    return failure<void>(eve::DiagnosticCode::InvalidArgument, "curve times must be finite, normalized, and strictly increasing", path + ".curves." + name);
                previous = key.time;
            }
        }
    }
    if (trail && (trail->maxSamples == 0 || !finiteNonNegative(trail->lifetime) ||
                  !finiteNonNegative(trail->minSampleDistance) || !finiteNonNegative(trail->teleportDistance)))
        return failure<void>(eve::DiagnosticCode::InvalidArgument, "invalid trail settings", "trail");
    if (trailBinding && (!trail || trailBinding->rootAttachment.empty() || trailBinding->tipAttachment.empty()))
        return failure<void>(eve::DiagnosticCode::InvalidArgument,
                             "trail attachment binding requires a trail and two non-empty attachment names", "trail");
    float previousEventTime = -1.f;
    for (const auto& event : events) {
        if (!std::isfinite(event.time) || event.time < 0.f || event.time > 1.f || event.time < previousEventTime || event.name.empty())
            return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                 "events require non-empty names and non-decreasing normalized times", "events");
        previousEventTime = event.time;
    }
    std::set<std::string> triggerNames;
    const auto validateTriggers = [&](const std::vector<std::string>& names) -> eve::Result<void> {
        for (const auto& name : names)
            if (name.empty() || !triggerNames.insert(name).second)
                return failure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "animation trigger names must be non-empty and map to exactly one action",
                                     "animationTriggers");
        return eve::Result<void>::success();
    };
    auto triggerStatus = validateTriggers(animationTriggers.play);
    if (!triggerStatus) return triggerStatus;
    triggerStatus = validateTriggers(animationTriggers.stop);
    if (!triggerStatus) return triggerStatus;
    triggerStatus = validateTriggers(animationTriggers.trailBreak);
    if (!triggerStatus) return triggerStatus;
    return eve::Result<void>::success();
}

float MeshVfxFloatCurve::evaluate(float normalizedTime) const noexcept {
    if (keys.empty()) return 0.f;
    const float time = std::clamp(normalizedTime, 0.f, 1.f);
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    const auto upper = std::upper_bound(keys.begin(), keys.end(), time,
                                        [](float value, const MeshVfxFloatKey& key) { return value < key.time; });
    const auto& right = *upper;
    const auto& left = *(upper - 1);
    const float alpha = (time - left.time) / (right.time - left.time);
    return left.value + (right.value - left.value) * alpha;
}

eve::Result<std::unique_ptr<MeshVfxAssetInstance>> MeshVfxAssetInstance::create(const MeshVfxAsset& asset) {
    auto valid = asset.validate();
    if (!valid) return eve::Result<std::unique_ptr<MeshVfxAssetInstance>>::failure(valid.status());
    try {
        return eve::Result<std::unique_ptr<MeshVfxAssetInstance>>::success(
            std::unique_ptr<MeshVfxAssetInstance>(new MeshVfxAssetInstance(asset)));
    } catch (const eve::Exception& error) {
        return failure<std::unique_ptr<MeshVfxAssetInstance>>(eve::DiagnosticCode::InvalidArgument, error.what(),
                                                              "layers.parameters");
    } catch (const std::exception& error) {
        return failure<std::unique_ptr<MeshVfxAssetInstance>>(eve::DiagnosticCode::Failed, error.what(),
                                                              "layers");
    }
}

MeshVfxAssetInstance::MeshVfxAssetInstance(const MeshVfxAsset& asset) {
    for (const auto& definition : asset.layers) {
        auto runtime = std::make_unique<MeshEffectInstance>(definition.style);
        runtime->setPlayback(definition.playback);
        for (const auto& [name, value] : definition.floatParameters) runtime->style().setFloat(name, value);
        for (const auto& [name, curve] : definition.floatCurves)
            runtime->style().setFloat(name, curve.evaluate(0.f));
        layers_.push_back(std::move(runtime));
        curves_.push_back(definition.floatCurves);
    }
    if (asset.trail) trail_ = std::make_unique<TrailEmitter>(*asset.trail);
    trailBinding_ = asset.trailBinding;
    events_ = asset.events;
    animationTriggers_ = asset.animationTriggers;
}

void MeshVfxAssetInstance::bindTarget(MeshEffectTargetHandle target) {
    for (auto& layer : layers_) layer->bindTarget(target);
}

void MeshVfxAssetInstance::play() noexcept {
    if (trail_) trail_->clear();
    pendingEvents_.clear();
    previousNormalizedTime_ = 0.f;
    for (const auto& event : events_)
        if (event.time == 0.f) pendingEvents_.push_back(event.name);
    for (auto& layer : layers_) layer->play();
}

void MeshVfxAssetInstance::stop(float fadeOutSeconds) {
    for (auto& layer : layers_)
        layer->stop(fadeOutSeconds < 0.f ? layer->playback().fadeOut : fadeOutSeconds);
    if (trail_) trail_->breakTrail();
}

void MeshVfxAssetInstance::update(float dtSeconds) {
    for (std::size_t index = 0; index < layers_.size(); ++index) {
        auto& layer = *layers_[index];
        layer.update(dtSeconds);
        const auto playback = layer.playback();
        const float cycle = playback.fadeIn + playback.duration + playback.fadeOut;
        const float normalized = cycle > 0.f ? std::clamp(layer.elapsed() / cycle, 0.f, 1.f) : 1.f;
        for (const auto& [name, curve] : curves_[index]) layer.style().setFloat(name, curve.evaluate(normalized));
    }
    if (trail_) trail_->update(dtSeconds);
    if (layers_.empty()) return;
    const auto playback = layers_.front()->playback();
    const float cycle = playback.fadeIn + playback.duration + playback.fadeOut;
    const float current = cycle > 0.f ? std::clamp(layers_.front()->elapsed() / cycle, 0.f, 1.f) : 1.f;
    const bool wrapped = playback.loop && current < previousNormalizedTime_;
    for (const auto& event : events_) {
        const bool crossed = wrapped ? (event.time > previousNormalizedTime_ || event.time <= current)
                                     : (event.time > previousNormalizedTime_ && event.time <= current);
        if (crossed) pendingEvents_.push_back(event.name);
    }
    previousNormalizedTime_ = current;
}

std::vector<std::string> MeshVfxAssetInstance::drainEvents() {
    std::vector<std::string> result;
    result.swap(pendingEvents_);
    return result;
}

eve::Result<TrailAppendResult> MeshVfxAssetInstance::sampleTrail(const eve::IAttachmentPointSource& source) {
    if (!trail_ || !trailBinding_)
        return failure<TrailAppendResult>(eve::DiagnosticCode::PreconditionViolation,
                                          "mesh VFX has no authored trail attachment binding", "trail");
    auto root = source.sampleAttachmentPoint(trailBinding_->rootAttachment, trailBinding_->rootOffset);
    if (!root) return eve::Result<TrailAppendResult>::failure(root.status());
    auto tip = source.sampleAttachmentPoint(trailBinding_->tipAttachment, trailBinding_->tipOffset);
    if (!tip) return eve::Result<TrailAppendResult>::failure(tip.status());
    const auto rootPoint = std::move(root).takeValue();
    const auto tipPoint = std::move(tip).takeValue();
    return eve::Result<TrailAppendResult>::success(
        trail_->append({rootPoint.x, rootPoint.y, rootPoint.z}, {tipPoint.x, tipPoint.y, tipPoint.z}));
}

MeshVfxAnimationDispatch MeshVfxAssetInstance::processAnimationEvents(const eve::IAnimationEventSource& source) {
    MeshVfxAnimationDispatch dispatch;
    const auto contains = [](const std::vector<std::string>& values, const std::string& name) {
        return std::find(values.begin(), values.end(), name) != values.end();
    };
    for (std::size_t index = 0; index < source.animationEventCount(); ++index) {
        const std::string name = source.animationEventName(index);
        if (contains(animationTriggers_.play, name)) {
            play();
            ++dispatch.played;
        } else if (contains(animationTriggers_.stop, name)) {
            stop();
            ++dispatch.stopped;
        } else if (contains(animationTriggers_.trailBreak, name)) {
            if (trail_) trail_->breakTrail();
            ++dispatch.trailBreaks;
        } else {
            continue;
        }
        ++dispatch.handled;
    }
    return dispatch;
}

TrailEmitter& MeshVfxAssetInstance::trail() {
    if (!trail_) throw eve::Exception("MeshVfxAssetInstance.trail: asset has no trail");
    return *trail_;
}

MeshVfxAssetSlot::MeshVfxAssetSlot(MeshVfxAsset asset) : asset_(std::move(asset)) {
    auto valid = asset_.validate();
    if (!valid) throw eve::Exception("%s", valid.status().describe().c_str());
}

eve::Result<std::uint64_t> MeshVfxAssetSlot::reload(std::string_view json) {
    auto candidate = MeshVfxAsset::fromJson(json);
    if (!candidate) return eve::Result<std::uint64_t>::failure(candidate.status());
    if (revision_ == UINT64_MAX)
        return failure<std::uint64_t>(eve::DiagnosticCode::InvariantViolation, "mesh VFX asset revision exhausted");
    asset_ = std::move(candidate).takeValue();
    return eve::Result<std::uint64_t>::success(++revision_);
}

}  // namespace eve::stylize
