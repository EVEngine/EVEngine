#include "procgen/Procgen.h"
#include "procgen/ProcgenOwnership.h"

#include "procgen/Biome.h"

namespace eve::procgen {

eve::Result<ProcgenBiomeRulesHandleRef> Procgen::newBiomeRulesHandle() {
    return ownership_->biomeRules.emplace(std::make_unique<BiomeRules>());
}

}  // namespace eve::procgen
