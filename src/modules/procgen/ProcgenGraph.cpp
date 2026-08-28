#include "procgen/Procgen.h"

#include "procgen/PointGraph.h"

namespace eve::procgen {

eve::Result<ProcgenPointGraphHandleRef> Procgen::newPointGraphHandle() {
    return pointGraphs_.emplace(std::make_unique<PointGraph>());
}

}  // namespace eve::procgen
