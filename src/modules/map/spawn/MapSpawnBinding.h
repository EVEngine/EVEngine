#pragma once
/** @file MapSpawnBinding.h
 * @brief map_spawn asset-admission face: canonical immutable loading descriptor, shared by runtime and authoring.
 */
#include <cstddef>
#include <memory>
#include <string>
#include "common/Export.h"
#include "common/Result.h"
namespace eve {
class Value;
}
namespace eve::map_spawn {
struct PixelTransform;
/** @brief Owning admitted eve.map-spawn-binding/v1 facts; no files, entities, callbacks or live map are retained.
 * @ownership Immutable strings/transform independent of source; borrowed getters expire at move/destruction.
 * @thread Content/authoring owner thread; parsing and reads are reentrant, invoke no external code or registry lookup.
 * @remarks Structural descriptor admission only: map/sidecar existence, id agreement and navigation layer geometry
 * remain canonical map preparation responsibilities. This is not a second persistent format or source catalogue.
 */
class EVENGINE_API_WORLD MapSpawnBinding final {
public:
    /** @brief Release local admitted facts only. */
    ~MapSpawnBinding();
    /** @brief Transfer admitted facts; moved-from getters must not be used. */
    MapSpawnBinding(MapSpawnBinding&&) noexcept;
    /** @brief Replace local facts by move; retains no input source references. */
    MapSpawnBinding& operator=(MapSpawnBinding&&) noexcept;
    MapSpawnBinding(const MapSpawnBinding&)            = delete;
    MapSpawnBinding& operator=(const MapSpawnBinding&) = delete;
    /** @brief Decode through the exact runtime loader binding codec, without opening files or publishing anything.
     * @return Owning token or canonical schema/version/unknown-field/URI/transform/index failure.
     * @remarks Only v1 is supported, unknown fields/future versions reject, no inferred legacy migration.
     * @cost Descriptor field validation, URI parsing and owning string allocation per explicit asset preparation.
     * @throws std::bad_alloc Allocation failed without source or observable world mutation.
     */
    [[nodiscard]] static Result<MapSpawnBinding> decode(const Value& source);
    /** @brief Borrow the admitted map identity until move/destruction. */
    [[nodiscard]] const std::string& mapId() const noexcept;
    /** @brief Borrow the admitted explicit project map URI until move/destruction. */
    [[nodiscard]] const std::string& mapUri() const noexcept;
    /** @brief Borrow the admitted explicit project sidecar URI until move/destruction. */
    [[nodiscard]] const std::string& spawnsUri() const noexcept;
    /** @brief Copy the finite positive isotropic pixel transform. */
    [[nodiscard]] PixelTransform transform() const noexcept;
    /** @brief Read the zero-based navigation layer index (default zero); does not inspect a live map. */
    [[nodiscard]] std::size_t navigationLayer() const noexcept;

private:
    struct Impl;
    explicit MapSpawnBinding(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::map_spawn
