#include "particles/ParticleEffect.h"

#include "common/Module.h"
#include "data/DataModule.h"
#include "data/JsonDocument.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "particles/ParticleConfig.h"
#include "particles/ParticleEmitter.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Stringifier.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <sstream>
#include <unordered_set>

namespace eve::particles {
namespace {

constexpr int kParticleEffectVersion = 1;

float asFloat(const Poco::Dynamic::Var& value, float fallback) {
    try {
        return value.convert<float>();
    } catch (...) {
        return fallback;
    }
}

int asInt(const Poco::Dynamic::Var& value, int fallback) {
    try {
        return value.convert<int>();
    } catch (...) {
        return fallback;
    }
}

bool asBool(const Poco::Dynamic::Var& value, bool fallback) {
    try {
        return value.convert<bool>();
    } catch (...) {
        return fallback;
    }
}

std::string asString(const Poco::Dynamic::Var& value) {
    try {
        return value.convert<std::string>();
    } catch (...) {
        return {};
    }
}

std::string objectJson(const Poco::JSON::Object::Ptr& object) {
    std::ostringstream out;
    Poco::JSON::Stringifier::stringify(Poco::Dynamic::Var(object), out, 0, 0);
    return out.str();
}

int objectBuffer(const Poco::JSON::Object::Ptr& object, int fallback) {
    if (!object || !object->has("buffer")) return fallback;
    return std::max(1, asInt(object->get("buffer"), fallback));
}

void readOffset(const Poco::JSON::Object::Ptr& object, float& x, float& y) {
    if (!object) return;
    if (object->has("offset")) {
        try {
            auto offset = object->getArray("offset");
            if (offset && offset->size() >= 2) {
                x = asFloat(offset->get(0), x);
                y = asFloat(offset->get(1), y);
            }
        } catch (...) {
        }
    }
    if (object->has("x")) x = asFloat(object->get("x"), x);
    if (object->has("y")) y = asFloat(object->get("y"), y);
}

std::string resolveAssetPath(const std::string& sourcePath, const std::string& referencedPath) {
    if (sourcePath.empty() || referencedPath.empty()) return referencedPath;
    const std::filesystem::path reference(referencedPath);
    if (reference.is_absolute()) return reference.generic_string();
    return (std::filesystem::path(sourcePath).parent_path() / reference).lexically_normal().generic_string();
}

bool applyParameters(ParticleEmitter* emitter, const Poco::JSON::Object::Ptr& parameters) {
    if (!emitter || !parameters) return false;
    for (const auto& parameter : *parameters)
        emitter->setFloatParameter(parameter.first, asFloat(parameter.second, 0.f));
    return true;
}

}  // namespace

ParticleEffect::~ParticleEffect() {
    for (auto& layer : layers_) {
        if (layer.emitter) layer.emitter->release();
        layer.emitter = nullptr;
    }
}

ParticleEffect* ParticleEffect::fromText(const std::string& json, const std::string& sourcePath, std::string* error) {
    auto fail = [&](const std::string& message) -> ParticleEffect* {
        if (error) *error = message;
        return nullptr;
    };

    auto*                               dataModule = eve::data::DataModule::create();
    std::string                         parseError;
    std::unique_ptr<data::JsonDocument> document(dataModule->decodeJson(json, &parseError));
    if (!document || !document->isObject())
        return fail(parseError.empty() ? "effect root must be an object" : parseError);

    auto root = document->object();
    if (!root || !root->has("type") || asString(root->get("type")) != "eve.particle-effect")
        return fail("effect type must be eve.particle-effect");
    const int version = root->has("version") ? asInt(root->get("version"), 0) : 0;
    if (version != kParticleEffectVersion)
        return fail("unsupported particle effect version: " + std::to_string(version));

    Poco::JSON::Array::Ptr emitterArray;
    try {
        emitterArray = root->getArray("emitters");
    } catch (...) {
    }
    if (!emitterArray || emitterArray->size() == 0) return fail("effect emitters must be a non-empty array");

    auto effect         = std::unique_ptr<ParticleEffect>(new ParticleEffect());
    effect->version_    = version;
    effect->sourcePath_ = sourcePath;

    Poco::JSON::Object::Ptr globalParameters;
    if (root->has("parameters")) {
        try {
            globalParameters = root->getObject("parameters");
        } catch (...) {
        }
        if (!globalParameters) return fail("effect parameters must be an object");
        for (const auto& parameter : *globalParameters)
            effect->parameters_[parameter.first] = asFloat(parameter.second, 0.f);
    }

    std::unordered_set<std::string> names;
    for (std::size_t i = 0; i < emitterArray->size(); ++i) {
        Poco::JSON::Object::Ptr layerObject;
        try {
            layerObject = emitterArray->getObject(static_cast<int>(i));
        } catch (...) {
        }
        if (!layerObject) return fail("emitter layer " + std::to_string(i) + " must be an object");

        const std::string name = layerObject->has("name") ? asString(layerObject->get("name")) : std::string();
        if (name.empty()) return fail("emitter layer " + std::to_string(i) + " requires a name");
        if (!names.insert(name).second) return fail("duplicate emitter layer name: " + name);

        Poco::JSON::Object::Ptr embeddedConfig;
        if (layerObject->has("emitter")) {
            try {
                embeddedConfig = layerObject->getObject("emitter");
            } catch (...) {
            }
            if (!embeddedConfig) return fail("emitter layer " + name + " has an invalid emitter object");
        }
        const std::string configPath =
            layerObject->has("config") ? asString(layerObject->get("config")) : std::string();
        if (embeddedConfig && !configPath.empty())
            return fail("emitter layer " + name + " cannot define both emitter and config");
        if (!embeddedConfig && configPath.empty()) return fail("emitter layer " + name + " requires emitter or config");

        int buffer          = layerObject->has("buffer") ? asInt(layerObject->get("buffer"), 1000) : 1000;
        buffer              = objectBuffer(embeddedConfig, buffer);
        auto*       emitter = ParticleEmitter::createEmitter(std::max(1, buffer));
        std::string configError;
        bool        configured = false;
        if (embeddedConfig) {
            configured = applyConfigText(emitter, objectJson(embeddedConfig), &configError);
        } else {
            configured = loadConfigFile(emitter, resolveAssetPath(sourcePath, configPath), &configError);
        }
        if (!configured) {
            emitter->release();
            return fail("emitter layer " + name + ": " + configError);
        }

        applyParameters(emitter, globalParameters);
        if (layerObject->has("parameters")) {
            Poco::JSON::Object::Ptr layerParameters;
            try {
                layerParameters = layerObject->getObject("parameters");
            } catch (...) {
            }
            if (!layerParameters) {
                emitter->release();
                return fail("emitter layer " + name + " parameters must be an object");
            }
            applyParameters(emitter, layerParameters);
        }

        Layer layer;
        layer.name    = name;
        layer.emitter = emitter;
        readOffset(layerObject, layer.offsetX, layer.offsetY);
        const float localRotation = layerObject->has("rotation") ? asFloat(layerObject->get("rotation"), 0.f) : 0.f;
        layer.baseDirection       = emitter->getDirection() + localRotation;
        layer.baseLayer           = emitter->getLayer();
        layer.enabled             = layerObject->has("enabled") ? asBool(layerObject->get("enabled"), true) : true;
        emitter->setVisible(layer.enabled);
        if (!layer.enabled) emitter->stop();
        effect->layers_.push_back(std::move(layer));
    }

    effect->syncTransform();
    effect->syncLayer();
    if (error) error->clear();
    return effect.release();
}

ParticleEffect* ParticleEffect::fromFile(const std::string& path, std::string* error) {
    if (path.empty()) {
        if (error) *error = "empty effect path";
        return nullptr;
    }
    auto* filesystem = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!filesystem) filesystem = eve::filesystem::Filesystem::create();
    std::unique_ptr<eve::filesystem::FileData> data;
    try {
        data.reset(filesystem->read(path));
    } catch (...) {
        if (error) *error = "read failed: " + path;
        return nullptr;
    }
    if (!data || data->getSize() == 0) {
        if (error) *error = "empty file: " + path;
        return nullptr;
    }
    return fromText(std::string(static_cast<const char*>(data->getData()), data->getSize()), path, error);
}

int ParticleEffect::getEmitterCount() const { return static_cast<int>(layers_.size()); }

std::string ParticleEffect::getEmitterName(int index) const {
    if (index < 0 || index >= getEmitterCount()) return {};
    return layers_[static_cast<std::size_t>(index)].name;
}

ParticleEmitter* ParticleEffect::getEmitter(int index) const {
    if (index < 0 || index >= getEmitterCount()) return nullptr;
    return layers_[static_cast<std::size_t>(index)].emitter;
}

ParticleEmitter* ParticleEffect::getEmitterByName(const std::string& name) const {
    auto layer = std::find_if(layers_.begin(), layers_.end(), [&](const Layer& value) { return value.name == name; });
    return layer == layers_.end() ? nullptr : layer->emitter;
}

void ParticleEffect::syncTransform() {
    const float c = std::cos(rotation_);
    const float s = std::sin(rotation_);
    for (auto& layer : layers_) {
        const float ox = layer.offsetX * scale_;
        const float oy = layer.offsetY * scale_;
        layer.emitter->setPosition(x_ + ox * c - oy * s, y_ + ox * s + oy * c);
        layer.emitter->setDirection(layer.baseDirection + rotation_);
    }
}

void ParticleEffect::syncLayer() {
    for (auto& value : layers_) value.emitter->setLayer(value.baseLayer + layer_);
}

void ParticleEffect::setPosition(float x, float y) {
    x_ = x;
    y_ = y;
    syncTransform();
}

void ParticleEffect::setRotation(float radians) {
    rotation_ = radians;
    syncTransform();
}

void ParticleEffect::setScale(float scale) {
    scale_ = std::max(0.f, scale);
    syncTransform();
}

void ParticleEffect::setLayer(int layer) {
    layer_ = layer;
    syncLayer();
}

void ParticleEffect::setVisible(bool visible) {
    visible_ = visible;
    for (auto& layer : layers_) layer.emitter->setVisible(visible && layer.enabled);
}

void ParticleEffect::start() {
    for (auto& layer : layers_)
        if (layer.enabled) layer.emitter->start();
}

void ParticleEffect::stop() {
    for (auto& layer : layers_) layer.emitter->stop();
}

void ParticleEffect::pause() {
    for (auto& layer : layers_)
        if (layer.enabled) layer.emitter->pause();
}

void ParticleEffect::reset() {
    for (auto& layer : layers_) layer.emitter->reset();
}

bool ParticleEffect::emit(const std::string& emitterName, int count) {
    auto* emitter = getEmitterByName(emitterName);
    if (!emitter || count <= 0) return false;
    emitter->emit(count);
    return true;
}

void ParticleEffect::setFloatParameter(const std::string& name, float value) {
    if (name.empty()) return;
    parameters_[name] = value;
    for (auto& layer : layers_) layer.emitter->setFloatParameter(name, value);
}

float ParticleEffect::getFloatParameter(const std::string& name) const {
    auto parameter = parameters_.find(name);
    return parameter == parameters_.end() ? 0.f : parameter->second;
}

bool ParticleEffect::hasFloatParameter(const std::string& name) const {
    return parameters_.find(name) != parameters_.end();
}

}  // namespace eve::particles
