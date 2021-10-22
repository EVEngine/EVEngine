#include "mouse/Mouse.h"
#include "mouse/sdl/Mouse.h"
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::mouse {

Module_IMPL(Mouse, new sdl::Mouse());

Mouse::~Mouse() {}


void Mouse::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Mouse::create, false);
    expose(cls);
}

void Mouse::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Mouse::getName);
    cls.addFunc("getX", &Mouse::getX);
}

}  // namespace eve::mouse
