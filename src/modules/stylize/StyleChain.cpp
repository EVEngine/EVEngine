#include "stylize/StyleChain.h"
#include <algorithm>
#include <cmath>
#include <glm/vec4.hpp>
#include <limits>
#include <utility>
#include "common/Exception.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "stylize/StyleInstance.h"
#include "stylize/StylePass.h"
#include "stylize/StyleRecipe.h"
#include "stylize/StyleShaders.h"

// StyleChain.cpp
namespace eve::stylize {

void StyleChain::clear() { passes_.clear(); }

void StyleChain::add(StylePass* pass) {
    if (!pass) throw eve::Exception("StyleChain.add: null pass");
    passes_.push_back(pass);
}

StylePass* StyleChain::getPass(int index) const {
    if (index < 0 || index >= int(passes_.size())) return nullptr;
    return passes_[size_t(index)];
}

void StyleChain::apply(graphics::Graphics* gfx, graphics::Texture* source, graphics::Canvas* dest,
                       graphics::Canvas* temp) {
    if (!gfx) throw eve::Exception("StyleChain.apply: null graphics");
    if (!source) throw eve::Exception("StyleChain.apply: null source");
    if (!dest) throw eve::Exception("StyleChain.apply: null dest");
    if (passes_.empty()) throw eve::Exception("StyleChain.apply: no passes");

    if (passes_.size() == 1) {
        passes_[0]->applyTo(gfx, source, dest);
        return;
    }
    if (!temp) throw eve::Exception("StyleChain.apply: temp canvas required for multi-pass");
    if (!temp->getTexture()) throw eve::Exception("StyleChain.apply: temp canvas has no sampleable texture");
    if (!dest->getTexture()) throw eve::Exception("StyleChain.apply: dest canvas has no sampleable texture");

    // Alternate temp / dest. Final result must live in dest.
    // n even: … → temp → dest
    // n odd : … → dest → temp, then blit temp → dest
    graphics::Texture* in = source;
    const int          n  = int(passes_.size());
    for (int i = 0; i < n; ++i) {
        graphics::Canvas* out = (i & 1) ? dest : temp;
        passes_[size_t(i)]->applyTo(gfx, in, out);
        in = out->getTexture();
        if (!in) throw eve::Exception("StyleChain.apply: intermediate texture missing");
    }
    if ((n & 1) != 0) {
        // Last write was temp — blit into dest (no style shader).
        graphics::Canvas* prev = gfx->getCanvas();
        gfx->setCanvas(dest);
        gfx->drawTexturedRect(temp->getTexture(), 0.f, 0.f, float(dest->getWidth()), float(dest->getHeight()),
                              glm::vec4(1.f, 1.f, 1.f, 1.f));
        gfx->setCanvas(prev);
    }
}

void StyleChain::applyCanvas(graphics::Graphics* gfx, graphics::Canvas* source, graphics::Canvas* dest,
                             graphics::Canvas* temp) {
    if (!source) throw eve::Exception("StyleChain.applyCanvas: null source");
    graphics::Texture* tex = source->getTexture();
    if (!tex) throw eve::Exception("StyleChain.applyCanvas: source has no texture");
    apply(gfx, tex, dest, temp);
}

}  // namespace eve::stylize

// StyleInstance.cpp
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
    const float defaultValue = getParamDefault(name);
    const auto  it           = overrides_.find(name);
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

// StylePass.cpp
namespace eve::stylize {

StylePass::StylePass(const std::string& style, graphics::Shader* shader) : style_(style), shader_(shader) {
    if (!shader_) throw eve::Exception("StylePass: null shader for style '%s'", style.c_str());
    if (const StyleDefinition* def = findStyleDefinition(style_)) {
        desc_.stage    = def->stage;
        desc_.priority = def->priority;
        if (def->depth) desc_.inputs |= graphics::PostInputDepth;
        if (def->normal) desc_.inputs |= graphics::PostInputNormal;
    }
}

std::string StylePass::getStage() const { return graphics::postEffectStageName(desc_.stage); }

bool StylePass::requiresInput(const std::string& input) const {
    if (input == "color") return (desc_.inputs & graphics::PostInputColor) != 0;
    if (input == "depth") return (desc_.inputs & graphics::PostInputDepth) != 0;
    if (input == "normal") return (desc_.inputs & graphics::PostInputNormal) != 0;
    if (input == "motion") return (desc_.inputs & graphics::PostInputMotion) != 0;
    if (input == "objectId") return (desc_.inputs & graphics::PostInputObjectId) != 0;
    return false;
}

bool StylePass::hasParam(const std::string& name) const { return shader_ && shader_->hasUniform(name); }

void StylePass::setFloat(const std::string& name, float value) {
    if (!shader_) throw eve::Exception("StylePass.setFloat: null shader");
    shader_->sendFloat(name, value);
}

float StylePass::getFloat(const std::string& name) const {
    if (!shader_) throw eve::Exception("StylePass.getFloat: null shader");
    float v = 0.f;
    if (shader_->getFromVar(name, &v, sizeof(v)) != int(sizeof(v)))
        throw eve::Exception("StylePass.getFloat: missing param '%s'", name.c_str());
    return v;
}

void StylePass::setTime(float seconds) {
    if (hasParam("time")) setFloat("time", seconds);
}

float StylePass::getTime() const {
    if (!hasParam("time")) return 0.f;
    return getFloat("time");
}

void StylePass::uploadScreenUniforms(int width, int height) {
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (hasParam("texelW")) setFloat("texelW", 1.f / float(width));
    if (hasParam("texelH")) setFloat("texelH", 1.f / float(height));
    if (hasParam("screenW")) setFloat("screenW", float(width));
    if (hasParam("screenH")) setFloat("screenH", float(height));
}

void StylePass::apply(graphics::Graphics* gfx, graphics::Texture* source) {
    if (!gfx) throw eve::Exception("StylePass.apply: null graphics");
    if (!source) throw eve::Exception("StylePass.apply: null source texture");
    if (!shader_) throw eve::Exception("StylePass.apply: null shader");

    const int w = source->getWidth();
    const int h = source->getHeight();
    uploadScreenUniforms(w, h);

    // Prefer destination canvas size for pixel grid when available.
    if (auto* dst = gfx->getCanvas()) {
        if (hasParam("screenW")) setFloat("screenW", float(dst->getWidth()));
        if (hasParam("screenH")) setFloat("screenH", float(dst->getHeight()));
    } else {
        if (hasParam("screenW")) setFloat("screenW", float(gfx->getWidth()));
        if (hasParam("screenH")) setFloat("screenH", float(gfx->getHeight()));
    }

    const float dw = gfx->getCanvas() ? float(gfx->getCanvas()->getWidth()) : float(gfx->getWidth());
    const float dh = gfx->getCanvas() ? float(gfx->getCanvas()->getHeight()) : float(gfx->getHeight());

    gfx->drawTexturedRectShader(source, shader_, 0.f, 0.f, dw, dh, glm::vec4(1.f, 1.f, 1.f, 1.f));
}

void StylePass::applyCanvas(graphics::Graphics* gfx, graphics::Canvas* source) {
    if (!source) throw eve::Exception("StylePass.applyCanvas: null canvas");
    graphics::Texture* tex = source->getTexture();
    if (!tex) throw eve::Exception("StylePass.applyCanvas: canvas has no sampleable texture");
    apply(gfx, tex);
}

void StylePass::applyTo(graphics::Graphics* gfx, graphics::Texture* source, graphics::Canvas* dest) {
    if (!gfx) throw eve::Exception("StylePass.applyTo: null graphics");
    if (!dest) throw eve::Exception("StylePass.applyTo: null dest");
    graphics::Canvas* prev = gfx->getCanvas();
    gfx->setCanvas(dest);
    apply(gfx, source);
    gfx->setCanvas(prev);
}

void StylePass::applyCanvasTo(graphics::Graphics* gfx, graphics::Canvas* source, graphics::Canvas* dest) {
    if (!source) throw eve::Exception("StylePass.applyCanvasTo: null source");
    graphics::Texture* tex = source->getTexture();
    if (!tex) throw eve::Exception("StylePass.applyCanvasTo: source has no sampleable texture");
    applyTo(gfx, tex, dest);
}

}  // namespace eve::stylize

// StyleRecipe.cpp
namespace eve::stylize {

StyleRecipe::~StyleRecipe() = default;

void StyleRecipe::clear() {
    instances_.clear();
    passes_.clear();
    stage_.clear();
    compiled_ = false;
}

void StyleRecipe::add(StyleInstance* instance) {
    if (!instance) throw eve::Exception("StyleRecipe.add: null instance");
    instances_.push_back(instance);
    compiled_ = false;
}

StyleInstance* StyleRecipe::getStyle(int index) const {
    if (index < 0 || index >= int(instances_.size())) return nullptr;
    return instances_[size_t(index)];
}

void StyleRecipe::compile(graphics::Graphics* gfx) {
    if (!gfx) throw eve::Exception("StyleRecipe.compile: null graphics");
    if (instances_.empty()) throw eve::Exception("StyleRecipe.compile: no styles");

    if (graphics_ != gfx) {
        scratch_       = nullptr;
        scratchWidth_  = 0;
        scratchHeight_ = 0;
    }
    std::stable_sort(instances_.begin(), instances_.end(),
                     [](StyleInstance* a, StyleInstance* b) { return a->getPriority() < b->getPriority(); });
    stage_ = instances_.front()->getStage();
    passes_.clear();
    for (StyleInstance* instance : instances_) {
        if (instance->getStage() != stage_)
            throw eve::Exception("StyleRecipe.compile: cannot mix stages '%s' and '%s'", stage_.c_str(),
                                 instance->getStage().c_str());
        passes_.emplace_back(instance->newPass(gfx));
    }
    graphics_ = gfx;
    compiled_ = true;
}

void StyleRecipe::ensureScratch(graphics::Graphics* gfx, graphics::Canvas* dest) {
    if (passes_.size() < 2) return;
    const int width  = dest->getWidth();
    const int height = dest->getHeight();
    if (graphics_ != gfx || !scratch_ || scratchWidth_ != width || scratchHeight_ != height) {
        scratch_       = gfx->newCanvas(width, height);
        scratchWidth_  = width;
        scratchHeight_ = height;
        graphics_      = gfx;
    }
}

void StyleRecipe::apply(graphics::Graphics* gfx, graphics::Texture* source, graphics::Canvas* dest) {
    if (!compiled_) throw eve::Exception("StyleRecipe.apply: compile first");
    if (graphics_ != gfx) throw eve::Exception("StyleRecipe.apply: compiled for another Graphics");
    if (!source || !dest) throw eve::Exception("StyleRecipe.apply: null source or destination");
    ensureScratch(gfx, dest);

    StyleChain chain;
    for (const auto& pass : passes_) chain.add(pass.get());
    chain.apply(gfx, source, dest, scratch_);
}

void StyleRecipe::applyCanvas(graphics::Graphics* gfx, graphics::Canvas* source, graphics::Canvas* dest) {
    if (!source || !source->getTexture()) throw eve::Exception("StyleRecipe.applyCanvas: source has no texture");
    apply(gfx, source->getTexture(), dest);
}

}  // namespace eve::stylize
