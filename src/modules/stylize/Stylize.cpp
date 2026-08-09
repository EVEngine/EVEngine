#include "stylize/Stylize.h"

#include "stylize/ImageStylize.h"
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
    return style == "cartoon" || style == "ink";
}

StylePass *Stylize::newPass(graphics::Graphics *gfx, const std::string &style) {
    graphics::Shader *sh = createPostShader(gfx, style);
    return new StylePass(style, sh);
}

graphics::Shader *Stylize::newPostShader(graphics::Graphics *gfx, const std::string &style) {
    return createPostShader(gfx, style);
}

graphics::Shader *Stylize::newMeshShader(graphics::Graphics *gfx, const std::string &style) {
    return createMeshShader(gfx, style);
}

image::ImageData *Stylize::processImage(image::ImageData *src, const std::string &style) {
    if (!hasStyle(style)) throw eve::Exception("Stylize.processImage: unknown style '%s'", style.c_str());
    return processImageCpu(src, style);
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
    pass.addFunc("getShader", &StylePass::getShader);
}

void Stylize::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Stylize::getName);
    cls.addFunc("getStyleCount", &Stylize::getStyleCount);
    cls.addFunc("getStyleId", &Stylize::getStyleId);
    cls.addFunc("hasStyle", &Stylize::hasStyle);
    cls.addFunc("hasMeshStyle", &Stylize::hasMeshStyle);
    cls.addFunc("newPass", &Stylize::newPass);
    cls.addFunc("newPostShader", &Stylize::newPostShader);
    cls.addFunc("newMeshShader", &Stylize::newMeshShader);
    cls.addFunc("processImage", &Stylize::processImage);
}

}  // namespace eve::stylize
