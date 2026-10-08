#pragma once
#include "common/Export.h"

#include "common/Result.h"

#include <cstddef>
#include <vector>

namespace eve::particles {

class ParticleEmitter;

/**
 * @brief Soft pool of idle emitters for short-lived gameplay VFX.
 *
 * Acquire returns a reset emitter with at least `minBuffer` capacity. Recycle
 * stops the emitter, clears live particles, and returns it to the idle list.
 * Destroying the pool releases every retained emitter.
 *
 * @ownership The pool owns idle emitters. Callers borrow acquired emitters until
 *            they recycle or release them outside the pool.
 * @thread Owner thread only.
 */
class EVENGINE_API_DOMAINS ParticleEmitterPool {
public:
    /** @brief Particle emitter pool. */
    ParticleEmitterPool() = default;
    /** @brief Particle emitter pool. */
    ~ParticleEmitterPool();

    ParticleEmitterPool(const ParticleEmitterPool&)            = delete;
    ParticleEmitterPool& operator=(const ParticleEmitterPool&) = delete;

    /**
     * @brief Borrow a reset emitter with capacity >= minBuffer.
     * @return Borrowed emitter pointer on success.
     */
    [[nodiscard]] Result<ParticleEmitter*> acquire(int minBuffer = 64);

    /**
     * @brief Return a previously acquired emitter to the idle list.
     * @return Applied when recycled, InvalidArgument when emitter is null.
     */
    [[nodiscard]] Result<void> recycle(ParticleEmitter* emitter);

    /** @brief Release every idle emitter and clear the pool. */
    void clear();

    /** @brief Number of idle emitters currently retained. */
    [[nodiscard]] std::size_t idleCount() const { return idle_.size(); }

private:
    std::vector<ParticleEmitter*> idle_;
};

}  // namespace eve::particles
