#pragma once

/** @file ActionPrefabBlock.h @brief Typed payload contract for prefab-spawn action states. */

#include "action/ActionSpatialBlock.h"
#include "common/Time.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace eve::action {

/** @brief Authority responsible for ending a spawned prefab instance. */
enum class PrefabSpawnLifecycle : std::uint8_t {
    /** @brief The action block recycles the instance on Exit or interruption. */
    RecycleOnBlockExit,
    /** @brief The action block recycles the instance after customDuration. */
    CustomDuration,
    /** @brief Ownership is transferred to the prefab instance service after spawning. */
    Independent,
};

/**
 * @brief Fully validated, owning settings for one prefab-spawn state block.
 *
 * This is the renderer-neutral contract shared by editor validation and the
 * runtime provider. It stores no entity, scene-node, resource, or bone pointer.
 */
struct ActionPrefabSpawnBinding {
    std::string           uri;
    PrefabSpawnLifecycle lifecycle = PrefabSpawnLifecycle::RecycleOnBlockExit;
    Duration              customDuration = Duration::zero();
    ActionSpatialBinding  spatial;

    auto operator<=>(const ActionPrefabSpawnBinding&) const = default;

    /**
     * @brief Decode a prefab-spawn payload without mutating runtime state.
     * @param payload Owning action payload containing uri, lifecycle and spatial fields.
     * @return Validated settings or a stable field-path diagnostic.
     * @remarks lifecycle defaults to recycle_on_block_exit. custom_duration requires
     *          a positive finite customDurationSeconds value.
     */
    [[nodiscard]] static Result<ActionPrefabSpawnBinding> fromPayload(const Value::Object& payload);
};

/** @brief Return the stable serialized spelling for a prefab lifecycle. */
[[nodiscard]] std::string_view prefabSpawnLifecycleName(PrefabSpawnLifecycle lifecycle) noexcept;

}  // namespace eve::action
