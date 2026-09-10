#include "graphics/CanvasScriptBindings.h"
#include "graphics/Canvas.h"
#include "graphics/Texture.h"
#include "image/ImageData.h"
#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <memory>
#include <exception>
namespace eve::graphics {
void exposeCanvasScriptBindings(ssq::Table& table) {
    const auto vm = table.getHandle();
    auto cls = table.addClass<Canvas>("Canvas", std::function<Canvas*()>([]() -> Canvas* { return nullptr; }), true);
    cls.addFunc("getWidth", &Canvas::getWidth);
    cls.addFunc("getHeight", &Canvas::getHeight);
    cls.addFunc("getTexture", &Canvas::getTexture);
    cls.addFunc("newHDRImageData", &Canvas::newHDRImageData);
    // Synchronous owner-thread snapshot; script owns the resulting ImageData.
    cls.addFunc("readPixels", [vm](Canvas* self) {
        try {
            if (!self) throw std::runtime_error("canvas must not be null");
            std::unique_ptr<image::ImageData> pixels(self->newImageData());
            if (!pixels) throw std::runtime_error("canvas has no readable RGBA8 pixels");
            auto status = Result<void>::success();
            auto result = eve::script::projectStatusResult(vm, status.status(), true, true);
            result.set("value", pixels.get());
            result.set("ownership", std::string("script-owned"));
            pixels.release();
            return result;
        } catch (const std::exception& error) {
            return eve::script::projectResult(vm, Result<void>::failure(
                Diagnostic::error(DiagnosticCode::Failed, error.what(), "canvas.readPixels")));
        }
    });
}
} // namespace eve::graphics
