#include "procgen/Procgen.h"

#include "procgen/ShapeGrammar.h"

namespace eve::procgen {

ShapeGrammar* Procgen::newShapeGrammar() { return new ShapeGrammar(); }

}  // namespace eve::procgen
