#include "window/Window.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include "window/sdl/Window.h"

namespace eve {

namespace window {

Module_IMPL(Window)

Window* Window::create() {
    auto p = registered_modules.find(name);
    if (p != registered_modules.end()) return (Window*)(p->second);
    auto n                   = new sdl::Window();
    registered_modules[name] = n;
    return n;
}

void Window::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Window::create);
    expose(cls);
}

void Window::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Window::getName);
}

}  // namespace window
}  // namespace eve