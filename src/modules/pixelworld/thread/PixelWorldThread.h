#pragma once
#include "common/Export.h"


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
class EVENGINE_API_PLATFORM JobSystemPixelScheduler final : public eve::pixelworld::PixelWorkScheduler {
public:
    /**
     * @brief Borrow a running scheduler.
     * @param jobs JobSystem that must outlive this adapter and all calls.
     */
    explicit JobSystemPixelScheduler(eve::thread::JobSystem& jobs) noexcept;

    /** @brief Parallel for. */
    void parallelFor(std::size_t workItems,
                     /** @brief Void. */
                     const std::function<void(std::size_t)>& body) override;
    /** @brief Worker count. */
    [[nodiscard]] std::size_t workerCount() const noexcept override;

private:
    eve::thread::JobSystem* jobs_ = nullptr;
};

}  // namespace eve::pixelworld_thread
