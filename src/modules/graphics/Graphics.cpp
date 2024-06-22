#include "graphics/Graphics.h"
#include "graphics/vulkan/Graphics.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::graphics {

Module_IMPL(Graphics, new vulkan::Graphics());

void Graphics::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Graphics::create, false);
    expose(cls);
}


void Graphics::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Graphics::getName);
    cls.addFunc("reset", &Graphics::reset);
    cls.addFunc("present", &Graphics::present);
}


void Graphics::reset() {
}


}  // namespace eve::graphics
