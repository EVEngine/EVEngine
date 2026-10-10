#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include "common/Export.h"
#include "common/Result.h"
namespace eve {
class Value;
}
namespace eve::graphics {
struct SkyAtmosphereParameters;
}
namespace eve::daynight {
struct SkyRuntimeSettings;
/** @brief Immutable validated atmospheric sky profile, schema eve.sky-profile versions 1 through 6.
 * @details Decode
 * owns all data and rejects unknown/missing fields and unsupported versions/modes. Version 2 adds analytic background
 * height fog. Version 1 is explicitly migrated with fog disabled, preserving its original linear sky output. Version 3
 * adds an explicit VFS wisps asset reference; earlier versions keep the layer disabled. Version 4 adds cloudMotion;
 * versions 1 through 3 migrate with zero cloud speed, preserving frozen source phase.
 * Version 5 adds directional fog fading. Version 6 requires an explicit uint32 proceduralSeed; null wispsAsset
 * selects procedural-v1, while a nonempty path selects strict external loading without automatic recovery.
 * Asset I/O occurs during
 * preparation, after profile decode. This is not a full UDS preset or a simulation save.
 * @thread Concurrent
 * immutable reads are safe. No callbacks or provider pointers.
 */
class EVENGINE_API_WORLD SkyProfile {
public:
    /** @brief Validate a complete profile without creating GPU resources or changing any runtime.
     * @return Owned immutable profile, or InvalidArgument with no observable partial import. */
    [[nodiscard]] static Result<SkyProfile> decode(const Value& document);
    /** @brief Borrow runtime settings, valid for this profile's lifetime. */
    [[nodiscard]] const SkyRuntimeSettings& settings() const noexcept;
    /** @brief Borrow optical settings, valid for this profile's lifetime. */
    [[nodiscard]] const graphics::SkyAtmosphereParameters& atmosphere() const noexcept;
    /** @brief Explicit procedural-v1 seed when version 6 selects a null asset; absent for loaded/legacy profiles. */
    [[nodiscard]] std::optional<uint32_t> proceduralSeed() const noexcept;
    /** @brief Borrow optional VFS wisps manifest path; empty for versions 1/2 or explicitly selected procedural assets.
     */
    [[nodiscard]] const std::string& wispsAsset() const noexcept;

private:
    struct Impl;
    explicit SkyProfile(std::shared_ptr<const Impl> data);
    std::shared_ptr<const Impl> data_;
};
}  // namespace eve::daynight
