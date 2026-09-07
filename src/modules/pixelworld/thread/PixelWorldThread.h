#pragma once

#include "pixelworld/PixelWorld.h"

namespace eve::thread {
class JobSystem;
}

namespace eve::pixelworld_thread {

/**
 * @brief Synchronous PixelWorld candidate scheduler backed by engine JobSystem.
 *
 * The adapter borrows JobSystem for its whole lifetime and never stops or owns it.
 * `parallelFor` joins every submitted job before returning. Worker failure falls
 * back to a deterministic serial overwrite of all index-owned result slots.
 */
class JobSystemPixelScheduler final : public eve::pixelworld::PixelWorkScheduler {
public:
    /**
     * @brief Borrow a running scheduler.
     * @param jobs JobSystem that must outlive this adapter and all calls.
     */
    explicit JobSystemPixelScheduler(eve::thread::JobSystem& jobs) noexcept;

    void parallelFor(std::size_t workItems,
                     const std::function<void(std::size_t)>& body) override;
    [[nodiscard]] std::size_t workerCount() const noexcept override;

private:
    eve::thread::JobSystem* jobs_ = nullptr;
};

}  // namespace eve::pixelworld_thread
