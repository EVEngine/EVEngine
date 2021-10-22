#include "cmdline.h"
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve
{

Module_IMPL(Cmdline, new Cmdline());

Cmdline::Cmdline() {}


void Cmdline::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Cmdline::create, false);
    expose(cls);
}

void Cmdline::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Cmdline::getName);
}

} // namespace eve



