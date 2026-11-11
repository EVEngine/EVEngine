#pragma once
/** @file PathfinderState.h
 * @brief map module-interface: detached native navigation-provider checkpoint.
 * @remarks Persistent data face map.pathfinder-state v1; not a replacement for map grid assets or live binding.
 */
#include <memory>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Value.h"

namespace eve::map {
class Pathfinder;
/** @brief Immutable provider state, including pending layer synchronization and derived cache revisions.
 * @ownership Owns detached CPU facts; does not retain the source provider, layer, Table or callbacks.
 * @thread Capture and restoration run synchronously on the map owner thread without callbacks.
 */
class EVENGINE_API_WORLD PathfinderState {
public:
    /** @brief Release detached facts. */
    ~PathfinderState();
    /** @brief Transfer detached facts. */
    PathfinderState(PathfinderState&&) noexcept;
    /** @brief Replace detached facts. */
    PathfinderState& operator=(PathfinderState&&) noexcept;
    PathfinderState(const PathfinderState&)            = delete;
    PathfinderState& operator=(const PathfinderState&) = delete;
    /** @brief Whether restoration requires an explicitly supplied map layer. */
    [[nodiscard]] bool bound() const noexcept;

    /** @brief Encode exact cached grid, pending layer inputs, capture history and cached field as v1.
     * @return Owning map.pathfinder-state value, or invalid/nonfinite/oversized native facts.
     * @remarks Preserves blocked costs and unreachable field cells; does not synchronize or rebuild caches.
     * @ownership No layer pointers, provenance tokens, services or callbacks are serialized.
     * @thread Immutable CPU encoding; caller synchronizes moves. No callbacks.
     * @cost Copies complete grid/history/cache-sized facts per explicit save boundary.
     * @throws std::bad_alloc Source and live providers remain unchanged.
     */
    [[nodiscard]] Result<Value> encodeState() const;
    /** @brief Admit strict map.pathfinder-state v1 without creating a provider or binding a layer.
     * @param value Synchronously borrowed tree; the result owns independent CPU facts.
     * @return Detached state or unknown schema/fields/version, dimensions, buffers or value failure.
     * @remarks Initial v1 has no implicit migration; each dimension and grid product is limited to 4M cells.
     * A real layer must still pass Pathfinder::prepareRestored; no old provenance/handle is reinstated.
     * @thread Pure CPU admission with no callbacks, clocks or live-world access.
     * @cost Validates and copies grid/history/cache-sized facts once per explicit load.
     * @throws std::bad_alloc Input and live providers remain unchanged.
     */
    [[nodiscard]] static Result<PathfinderState> prepareState(const Value& value);

private:
    friend class Pathfinder;
    struct Data;
    explicit PathfinderState(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};
}  // namespace eve::map
