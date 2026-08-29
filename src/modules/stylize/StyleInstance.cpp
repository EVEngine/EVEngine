#include "stylize/StyleInstance.h"

#include "stylize/StylePass.h"
#include "stylize/StyleShaders.h"

#include "common/Exception.h"
#include "graphics/Shader.h"

#include <algorithm>
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

int StyleInstance::getParamCount() const { return styleParamCount(style_); }

std::string StyleInstance::getStage() const {
    return graphics::postEffectStageName(findStyleDefinition(style_)->stage);
}

int StyleInstance::getPriority() const {
    return priority_.value_or(findStyleDefinition(style_)->priority);
}

bool StyleInstance::requiresInput(const std::string& input) const {
    const StyleDefinition* def = findStyleDefinition(style_);
    if (input == "color") return def->post;
    if (input == "depth") return def->depth;
    if (input == "normal") return def->normal;
    return false;
}

std::string StyleInstance::getParamName(int index) const { return styleParamName(style_, index); }

float StyleInstance::getParamDefault(const std::string& name) const { return requireParam(style_, name).defaultValue; }

float StyleInstance::getParamMin(const std::string& name) const { return requireParam(style_, name).minValue; }

float StyleInstance::getParamMax(const std::string& name) const { return requireParam(style_, name).maxValue; }

bool StyleInstance::hasParam(const std::string& name) const { return findStyleParameter(style_, name) != nullptr; }

bool StyleInstance::isOverridden(const std::string& name) const { return overrides_.find(name) != overrides_.end(); }

void StyleInstance::setFloat(const std::string& name, float value) {
    const StyleParameterDesc& desc = requireParam(style_, name);
    overrides_[name]               = std::clamp(value, desc.minValue, desc.maxValue);
}

float StyleInstance::getFloat(const std::string& name) const {
    const StyleParameterDesc& desc = requireParam(style_, name);
    const auto                it   = overrides_.find(name);
    return it == overrides_.end() ? desc.defaultValue : it->second;
}

void StyleInstance::reset(const std::string& name) {
    requireParam(style_, name);
    overrides_.erase(name);
}

void StyleInstance::resetAll() { overrides_.clear(); }

void StyleInstance::applyOverrides(graphics::Shader* shader) const {
    if (!shader) throw eve::Exception("StyleInstance: null shader");
    for (const auto& [name, value] : overrides_) {
        if (shader->hasUniform(name)) shader->sendFloat(name, value);
    }
}

StylePass* StyleInstance::newPass(graphics::Graphics* gfx) const {
    graphics::Shader* shader = createPostShader(gfx, style_);
    applyOverrides(shader);
    return new StylePass(style_, shader);
}

graphics::Shader* StyleInstance::newMeshShader(graphics::Graphics* gfx) const {
    graphics::Shader* shader = createMeshShader(gfx, style_);
    applyOverrides(shader);
    return shader;
}

}  // namespace eve::stylize
