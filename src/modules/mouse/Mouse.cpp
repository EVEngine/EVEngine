#include "mouse/Mouse.h"
#include "mouse/sdl/Mouse.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <vector>

namespace eve::mouse {

Module_IMPL(Mouse, new sdl::Mouse());

Mouse::~Mouse() {}


void Mouse::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Mouse::create, false);
    expose(cls);
}

void Mouse::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Mouse::getName);
    // Bind as float: SSQ/Squirrel treat C++ double as userdata, which breaks script arith.
    cls.addFunc("getX", [](Mouse *m) -> float { return m ? float(m->getX()) : 0.f; });
    cls.addFunc("getY", [](Mouse *m) -> float { return m ? float(m->getY()) : 0.f; });
    cls.addFunc("isDown", [](Mouse *m, int button) -> bool {
        if (!m) return false;
        return m->isDown(std::vector<int>{button});
    });
    cls.addFunc("isVisible", &Mouse::isVisible);
}

}  // namespace eve::mouse
