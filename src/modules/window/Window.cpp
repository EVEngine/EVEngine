#include "window/Window.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include "window/sdl/Window.h"

namespace eve {

namespace window {

Module_IMPL(Window);

Window* Window::create() {
    auto p = registered_modules.find(name);
    if (p != registered_modules.end()) return (Window*)(p->second);
    auto n                   = new sdl::Window();
    registered_modules[name] = n;
    return n;
}

void Window::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Window::create, false);
    expose(cls);

    auto settings = table.addClass("WindowSettings", ssq::Class::Ctor<WindowSettings()>());
}

void Window::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Window::getName);
    cls.addFunc("setSize", &Window::setSize);
    cls.addFunc("getWidth", &Window::getWidth);
    cls.addFunc("getHeight", &Window::getHeight);

    cls.addFunc("setWindowSettings", &Window::setWindowSettings);
    cls.addFunc("getWindowSettings", &Window::getWindowSettings);
    cls.addFunc("close", &Window::close);
}

}  // namespace window
}  // namespace eve