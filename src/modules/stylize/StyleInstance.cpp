#include "stylize/StyleInstance.h"

#include "stylize/StylePass.h"
#include "stylize/StyleShaders.h"

#include "common/Exception.h"
#include "graphics/Shader.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace eve::stylize {

namespace {
const StyleParameterDesc& requireParam(const std::string& style, const std::string& name) {
    const StyleParameterDesc* desc = findStyleParameter(style, name);
    if (!desc) throw eve::Exception("StyleInstance: style '%s' has no parameter '%s'", style.c_str(), name.c_str());
    return *desc;
}
}  // namespace

StyleInstance::StyleInstance(std::string style) : style_(std::move(style)) {
    if (!findStyleDefinition(style_)) throw eve::Exception("StyleInstance: unknown style '%s'", style_.c_str());
}

StyleInstance::StyleInstance(std::string style, std::map<std::string, float> defaults)
    : style_(std::move(style)), externalDefaults_(std::move(defaults)) {
    if (!style_.starts_with("asset://")) throw eve::Exception("External mesh styles require an asset URI");
    for (const auto& [name, value] : *externalDefaults_)
        if (name.empty() || !std::isfinite(value)) throw eve::Exception("Invalid external style parameter");
}

int StyleInstance::getParamCount() const {
    return externalDefaults_ ? int(externalDefaults_->size()) : styleParamCount(style_);
}

std::string StyleInstance::getStage() const {
    if (externalDefaults_) return graphics::postEffectStageName(graphics::PostEffectStage::AfterOpaque);
    return graphics::postEffectStageName(findStyleDefinition(style_)->stage);
}

int StyleInstance::getPriority() const {
    if (externalDefaults_) return priority_.value_or(40);
    return priority_.value_or(findStyleDefinition(style_)->priority);
}

bool StyleInstance::requiresInput(const std::string& input) const {
    if (externalDefaults_) return false;
    const StyleDefinition* def = findStyleDefinition(style_);
    if (input == "color") return def->post;
    if (input == "depth") return def->depth;
    if (input == "normal") return def->normal;
    return false;
}

std::string StyleInstance::getParamName(int index) const {
    if (externalDefaults_) {
        if (index < 0 || std::size_t(index) >= externalDefaults_->size())
            throw eve::Exception("Invalid parameter index");
        return std::next(externalDefaults_->begin(), index)->first;
    }
    return styleParamName(style_, index);
}

float StyleInstance::getParamDefault(const std::string& name) const {
    return externalDefaults_ ? externalDefaults_->at(name) : requireParam(style_, name).defaultValue;
}

float StyleInstance::getParamMin(const std::string& name) const {
    if (externalDefaults_) {
        if (!externalDefaults_->contains(name)) throw eve::Exception("Unknown external parameter");
        return -std::numeric_limits<float>::max();
    }
    return requireParam(style_, name).minValue;
}

float StyleInstance::getParamMax(const std::string& name) const {
    if (externalDefaults_) {
        if (!externalDefaults_->contains(name)) throw eve::Exception("Unknown external parameter");
        return std::numeric_limits<float>::max();
    }
    return requireParam(style_, name).maxValue;
}

bool StyleInstance::hasParam(const std::string& name) const {
    return externalDefaults_ ? externalDefaults_->contains(name) : findStyleParameter(style_, name) != nullptr;
}

bool StyleInstance::isOverridden(const std::string& name) const { return overrides_.find(name) != overrides_.end(); }

void StyleInstance::setFloat(const std::string& name, float value) {
    if (externalDefaults_) {
        if (!externalDefaults_->contains(name) || !std::isfinite(value))
            throw eve::Exception("Invalid external style parameter");
        overrides_[name] = value;
        return;
    }
    const StyleParameterDesc& desc = requireParam(style_, name);
    overrides_[name]               = std::clamp(value, desc.minValue, desc.maxValue);
}

float StyleInstance::getFloat(const std::string& name) const {
    const float               defaultValue = getParamDefault(name);
    const auto                it   = overrides_.find(name);
    return it == overrides_.end() ? defaultValue : it->second;
}

void StyleInstance::reset(const std::string& name) {
    getParamDefault(name);
    overrides_.erase(name);
}

void StyleInstance::resetAll() { overrides_.clear(); }

void StyleInstance::applyToShader(graphics::Shader* shader) const {
    if (!shader) throw eve::Exception("StyleInstance: null shader");
    for (const auto& [name, value] : overrides_) {
        if (shader->hasUniform(name)) shader->sendFloat(name, value);
    }
}

StylePass* StyleInstance::newPass(graphics::Graphics* gfx) const {
    if (externalDefaults_) throw eve::Exception("External style requires its package renderer");
    graphics::Shader* shader = createPostShader(gfx, style_);
    applyToShader(shader);
    return new StylePass(style_, shader);
}

graphics::Shader* StyleInstance::newMeshShader(graphics::Graphics* gfx) const {
    if (externalDefaults_) throw eve::Exception("External style requires its package renderer");
    graphics::Shader* shader = createMeshShader(gfx, style_);
    applyToShader(shader);
    return shader;
}

}  // namespace eve::stylize
