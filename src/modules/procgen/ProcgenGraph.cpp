#include "procgen/Procgen.h"

#include "procgen/PointGraph.h"

namespace eve::procgen {

PointGraph* Procgen::newPointGraph() { return new PointGraph(); }

}  // namespace eve::procgen
