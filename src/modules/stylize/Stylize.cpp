#include "stylize/Stylize.h"

#include "stylize/ImageStylize.h"
#include "stylize/StyleInstance.h"
#include "stylize/StyleShaders.h"

#include "common/Exception.h"
#include "graphics/Graphics.h"
#include "graphics/Shader.h"
#include "image/ImageData.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::stylize {

Module_IMPL(Stylize, new Stylize());

int Stylize::getStyleCount() const { return styleCount(); }

std::string Stylize::getStyleId(int index) const { return styleIdAt(index); }

bool Stylize::hasStyle(const std::string &style) const { return isKnownStyle(style); }

bool Stylize::hasMeshStyle(const std::string &style) const {
    return styleSupports(style, "mesh");
}

bool Stylize::supports(const std::string &style, const std::string &feature) const {
    return styleSupports(style, feature);
}

int Stylize::getStyleParamCount(const std::string &style) const {
    return styleParamCount(style);
}

std::string Stylize::getStyleParamName(const std::string &style, int index) const {
    return styleParamName(style, index);
}

StylePass *Stylize::newPass(graphics::Graphics *gfx, const std::string &style) {
    graphics::Shader *sh = createPostShader(gfx, style);
    return new StylePass(style, sh);
}

StylePass *Stylize::newPassFromShader(const std::string &styleId, graphics::Shader *shader) {
    if (!shader) throw eve::Exception("Stylize.newPassFromShader: null shader");
    return new StylePass(styleId, shader);
}

StyleChain *Stylize::newChain() { return new StyleChain(); }

graphics::Shader *Stylize::newPostShader(graphics::Graphics *gfx, const std::string &style) {
    return createPostShader(gfx, style);
}

graphics::Shader *Stylize::newMeshShader(graphics::Graphics *gfx, const std::string &style) {
    return createMeshShader(gfx, style);
}

image::ImageData *Stylize::processImage(image::ImageData *src, const std::string &style) {
    if (!hasStyle(style)) throw eve::Exception("Stylize.processImage: unknown style '%s'", style.c_str());
    if (!supports(style, "cpu"))
        throw eve::Exception("Stylize.processImage: style '%s' has no CPU technique", style.c_str());
    return processImageCpu(src, style);
}

StyleInstance *Stylize::newInstance(const std::string &style) {
    return new StyleInstance(style);
}

void Stylize::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Stylize::create, false);
    expose(cls);

    auto pass = table.addClass<StylePass>(
        "StylePass", std::function<StylePass *()>([]() -> StylePass * { return nullptr; }), true);
    pass.addFunc("getStyle", &StylePass::getStyle);
    pass.addFunc("hasParam", &StylePass::hasParam);
    pass.addFunc("setFloat", &StylePass::setFloat);
    pass.addFunc("getFloat", &StylePass::getFloat);
    pass.addFunc("setTime", &StylePass::setTime);
    pass.addFunc("getTime", &StylePass::getTime);
    pass.addFunc("apply", &StylePass::apply);
    pass.addFunc("applyCanvas", &StylePass::applyCanvas);
    pass.addFunc("applyTo", &StylePass::applyTo);
    pass.addFunc("applyCanvasTo", &StylePass::applyCanvasTo);
    pass.addFunc("getShader", &StylePass::getShader);
    pass.addFunc("getStage", &StylePass::getStage);
    pass.addFunc("getPriority", &StylePass::getPriority);
    pass.addFunc("setPriority", &StylePass::setPriority);
    pass.addFunc("requiresInput", &StylePass::requiresInput);

    auto instance = table.addClass<StyleInstance>(
        "StyleInstance", std::function<StyleInstance *()>([]() -> StyleInstance * { return nullptr; }),
        true);
    instance.addFunc("getStyle", &StyleInstance::getStyle);
    instance.addFunc("getStage", &StyleInstance::getStage);
    instance.addFunc("getPriority", &StyleInstance::getPriority);
    instance.addFunc("requiresInput", &StyleInstance::requiresInput);
    instance.addFunc("getParamCount", &StyleInstance::getParamCount);
    instance.addFunc("getParamName", &StyleInstance::getParamName);
    instance.addFunc("getParamDefault", &StyleInstance::getParamDefault);
    instance.addFunc("getParamMin", &StyleInstance::getParamMin);
    instance.addFunc("getParamMax", &StyleInstance::getParamMax);
    instance.addFunc("hasParam", &StyleInstance::hasParam);
    instance.addFunc("isOverridden", &StyleInstance::isOverridden);
    instance.addFunc("setFloat", &StyleInstance::setFloat);
    instance.addFunc("getFloat", &StyleInstance::getFloat);
    instance.addFunc("reset", &StyleInstance::reset);
    instance.addFunc("resetAll", &StyleInstance::resetAll);
    instance.addFunc("newPass", &StyleInstance::newPass);
    instance.addFunc("newMeshShader", &StyleInstance::newMeshShader);

    auto chain = table.addClass<StyleChain>(
        "StyleChain", std::function<StyleChain *()>([]() -> StyleChain * { return nullptr; }), true);
    chain.addFunc("clear", &StyleChain::clear);
    chain.addFunc("add", &StyleChain::add);
    chain.addFunc("getPassCount", &StyleChain::getPassCount);
    chain.addFunc("getPass", &StyleChain::getPass);
    chain.addFunc("apply", &StyleChain::apply);
    chain.addFunc("applyCanvas", &StyleChain::applyCanvas);
}

void Stylize::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Stylize::getName);
    cls.addFunc("getStyleCount", &Stylize::getStyleCount);
    cls.addFunc("getStyleId", &Stylize::getStyleId);
    cls.addFunc("hasStyle", &Stylize::hasStyle);
    cls.addFunc("hasMeshStyle", &Stylize::hasMeshStyle);
    cls.addFunc("supports", &Stylize::supports);
    cls.addFunc("getStyleParamCount", &Stylize::getStyleParamCount);
    cls.addFunc("getStyleParamName", &Stylize::getStyleParamName);
    cls.addFunc("newInstance", &Stylize::newInstance);
    cls.addFunc("newPass", &Stylize::newPass);
    cls.addFunc("newPassFromShader", &Stylize::newPassFromShader);
    cls.addFunc("newChain", &Stylize::newChain);
    cls.addFunc("newPostShader", &Stylize::newPostShader);
    cls.addFunc("newMeshShader", &Stylize::newMeshShader);
    cls.addFunc("processImage", &Stylize::processImage);
}

}  // namespace eve::stylize
