#include "window/Window.h"

#include "window/sdl/Window.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve
{

namespace window
{

Window* Window::create() {
    return new sdl::Window();
}

void Window::expose(ssq::Table& table) {
    auto cls = table.addClass("Window", Window::create);
    expose(cls);
}

void Window::expose(ssq::Class& cls) {
    cls.addFunc("getModuleType", &Window::getModuleType);
}

} // namespace window


}