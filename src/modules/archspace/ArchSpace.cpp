#include "archspace/ArchSpace.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::archspace {

Module_IMPL(ArchSpace, new ArchSpace());

void ArchSpace::expose(ssq::Table& table) { table.addClass(name, ArchSpace::create, false); }

void ArchSpace::expose(ssq::Class&) {}

}  // namespace eve::archspace
