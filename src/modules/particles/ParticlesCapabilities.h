#pragma once

namespace eve::particles {
/** @brief Registers particles capabilities. */
void registerParticlesCapabilities();
/** @brief Registers particles attack vfx executor. */
void registerParticlesAttackVfxExecutor();
/** @brief Unregisters particles attack vfx executor. */
void unregisterParticlesAttackVfxExecutor();
}
