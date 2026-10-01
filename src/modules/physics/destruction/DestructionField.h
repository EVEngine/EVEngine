#pragma once
#include "common/Export.h"

#include <cstdint>

namespace eve::physics {

/** @brief Runtime field kinds mirrored from Chaos Destruction Fields. */
enum class DestructionFieldKind : std::uint8_t {
    Anchor = 0,
    Strain = 1,
    Impulse = 2,
    Sleep = 3,
};

/** @brief Falloff applied from the field centre to its radius. */
enum class DestructionFieldFalloff : std::uint8_t {
    None = 0,
    Linear = 1,
};

/**
 * @brief One volumetric influence applied to a geometry-collection instance.
 *
 * Fields do not own instances. Strain accumulates on edges; Anchor pins bones;
 * Impulse hits detached dynamic bones; Sleep requests static settlement.
 */
struct EVENGINE_API_DOMAINS DestructionField {
    DestructionFieldKind kind = DestructionFieldKind::Strain;
    DestructionFieldFalloff falloff = DestructionFieldFalloff::Linear;
    float centerX = 0.f;
    float centerY = 0.f;
    float centerZ = 0.f;
    float radius = 1.f;
    float magnitude = 1.f;
    /** @brief Impulse direction (used only when kind == Impulse); zero uses +Y. */
    float dirX = 0.f;
    float dirY = 1.f;
    float dirZ = 0.f;
};

/** @brief Observable receipt for one checked field application. */
struct EVENGINE_API_DOMAINS FieldApplicationReceipt {
    int bonesAffected = 0;
    int edgesAffected = 0;
    /** @brief Sleep conversions skipped because of the per-step sleep budget. */
    int sleepsDeferred = 0;
};

}  // namespace eve::physics
