#pragma once
/** @file MapRPGPresentation.h
 * @brief map_rpg_spawn module-interface: read-only RPG/map presentation composition.
 * @remarks Inputs are explicit session/factory/renderer borrows; no ECS writes, events, scripts or tools.
 */
#include <cstddef>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::map_spawn {
class MapSpawnSession;
}
namespace eve::rpg_spawn {
class RPGSpawnFactory;
}
namespace eve::graphics {
class ISolidRectRenderer;
}
namespace eve::map_rpg_spawn {
/** @brief Explicit pixel-sized actor marker, independent of navigation footprint. */
struct ActorStyle {
    float width = 16, height = 16;
    float red = 0.8F, green = 0.25F, blue = 0.2F, alpha = 1;
};
/**
 * @brief Draw live source-owned RPG actors through the existing graphics primitive boundary.
 * @return Draw count; any source/actor/style/coordinate admission failure issues no draws.
 * @ownership Borrows inputs only during this call; owns a temporary draw batch, retains no actor pointers.
 * @thread Simulation/render owner thread after lifecycle reconciliation, outside ECS Views.
 * @remarks Captures all observations before renderer callbacks. Renderer exceptions propagate; emitted draws
 * cannot be rolled back. KeepUnits actors cease to be source-owned and need their game's normal presentation.
 * @cost Linear in source diagnostics and tracked actors, including temporary batch allocation per presentation.
 */
[[nodiscard]] EVENGINE_API_WORLD Result<std::size_t> drawActors(const map_spawn::MapSpawnSession& session,
                                                                const rpg_spawn::RPGSpawnFactory& actors,
                                                                graphics::ISolidRectRenderer&     renderer,
                                                                const ActorStyle&                 style);
}  // namespace eve::map_rpg_spawn
