#include "touch/Touch.h"
#include "touch/sdl/Touch.h"
#include <simplesquirrel/simplesquirrel.hpp>
namespace eve::touch {

Module_IMPL(Touch, new sdl::Touch());


void Touch::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Touch::create, false);
    expose(cls);
}

void Touch::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Touch::getName);
    cls.addFunc("getTouch", &Touch::getTouch);
}

}