#include "procgen/Procgen.h"

#include "procgen/Biome.h"

namespace eve::procgen {

BiomeRules* Procgen::newBiomeRules() { return new BiomeRules(); }

}  // namespace eve::procgen
