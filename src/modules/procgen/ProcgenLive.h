#pragma once

#include "common/Export.h"

namespace eve::procgen {

class Procgen;

/**
 * @brief Returns the currently live Procgen module instance, or nullptr.
 *
 * @ownership Borrowed; ModuleManager owns the instance when non-null.
 * @nullable Yes after shutdown or before the module is constructed.
 * @lifetime Valid until `~Procgen` clears the cached pointer; do not retain
 *           across module teardown.
 * @thread Main/composition and script owner thread only.
 * @reentrancy Must not re-enter module registration or shutdown.
 */
[[nodiscard]] Procgen* liveProcgen() noexcept;

/** @brief Publishes @p instance as the live Procgen owner (constructor path). */
void publishLiveProcgen(Procgen* instance) noexcept;

/** @brief Clears the live Procgen owner when it matches @p instance (destructor). */
void clearLiveProcgen(Procgen* instance) noexcept;

}  // namespace eve::procgen
