#include "procgen/Procgen.h"

#include "procgen/ShapeGrammar.h"

namespace eve::procgen {

eve::Result<ProcgenShapeGrammarHandleRef> Procgen::newShapeGrammarHandle() {
    return shapeGrammars_.emplace(std::make_unique<ShapeGrammar>());
}

}  // namespace eve::procgen
