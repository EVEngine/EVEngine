#include "touch/Touch.h"
#include "touch/sdl/Touch.h"
#include "common/Exception.h"
#include <simplesquirrel/simplesquirrel.hpp>
namespace eve::touch {

Module_IMPL(Touch, new sdl::Touch());

double Touch::getTouchX(int index) const {
    const auto &touches = getTouches();
    if (index < 0 || index >= int(touches.size()))
        throw Exception("Touch index out of range");
    return touches[size_t(index)].x;
}

double Touch::getTouchY(int index) const {
    const auto &touches = getTouches();
    if (index < 0 || index >= int(touches.size()))
        throw Exception("Touch index out of range");
    return touches[size_t(index)].y;
}

void Touch::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Touch::create, false);
    expose(cls);
}

void Touch::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Touch::getName);
    cls.addFunc("getTouchCount", &Touch::getTouchCount);
    cls.addFunc("getTouchX", [](Touch *t, int index) -> float {
        return t ? float(t->getTouchX(index)) : 0.f;
    });
    cls.addFunc("getTouchY", [](Touch *t, int index) -> float {
        return t ? float(t->getTouchY(index)) : 0.f;
    });
}

}