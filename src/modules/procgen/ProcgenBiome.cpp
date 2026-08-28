#include "procgen/Procgen.h"

#include "procgen/Biome.h"

namespace eve::procgen {

eve::Result<ProcgenBiomeRulesHandleRef> Procgen::newBiomeRulesHandle() {
    return biomeRules_.emplace(std::make_unique<BiomeRules>());
}

}  // namespace eve::procgen
