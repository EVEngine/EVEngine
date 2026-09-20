#pragma once
#include "common/Export.h"

#include "common/Result.h"
#include "common/Value.h"
#include "graphics/PbrSurface.h"
#include "graphics/VegetationMotion.h"

namespace eve::graphics {

/** @brief TVE Global Details values, independent of scene components and global shader state. */
struct VegetationDetailSettings {
    uint8_t              colorsLayer = 0, extrasLayer = 0, motionLayer = 0;
    float                globalColor = 1, globalAlpha = 1, globalOverlay = 1, globalWetness = 1;
    float                colorMaskMinimum = .4f, colorMaskMaximum = .6f;
    float                overlayMaskMinimum = .4f, overlayMaskMaximum = .6f;
    float                alphaThreshold  = .5f;
    float                perspectivePush = 0, perspectiveNoise = 0, perspectiveAngle = 1;
    std::array<float, 3> motionHighlight{1, 1, 1};
    float                bendingAmplitude = 1, bendingSpeed = 6, bendingScale = 2;
    float                flutterAmplitude = .1f, flutterSpeed = 20, flutterScale = 10;
    float                interactionAmplitude = 1;
};

/** @brief Detached render values produced from one validated Global Details publication.
 * PBR texture pointers remain borrowed with the same lifetime as the input surface.
 */
struct VegetationDetailRuntime {
    PbrSurface       surface;
    VegetationMotion motion;
    float            colorMaskMinimum = .4f, colorMaskMaximum = .6f;
    float            overlayMaskMinimum = .4f, overlayMaskMaximum = .6f;
    float            alphaThresholdOffset = 0;
};

/**
 * @brief Validate and project TVE Global Details into native PBR and motion snapshots.
 * @param baseSurface Existing material values and borrowed texture bindings.
 * @param baseMotion Existing explicit time, transform, noise and motion values.
 * @param settings Owning Global Details values.
 * @return A detached candidate, or InvalidArgument without changing either input.
 * @thread Worker-safe and reentrant; performs no IO, callbacks, clocks or GPU calls.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<VegetationDetailRuntime> configureVegetationDetails(const PbrSurface&               baseSurface,
                                                                         const VegetationMotion&         baseMotion,
                                                                         const VegetationDetailSettings& settings);

/**
 * @brief Encode one owning Global Details document using `eve.graphics.vegetation-details/1`.
 * @param settings Values to validate and encode.
 * @return Detached document, or InvalidArgument without publishing partial state.
 * @thread Worker-safe and reentrant.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<Value> snapshotVegetationDetails(const VegetationDetailSettings& settings);

/**
 * @brief Decode one exact Global Details document transactionally.
 * @param document Persistent `eve.graphics.vegetation-details/1` value.
 * @return Detached validated settings; unknown fields and versions fail.
 * @thread Worker-safe and reentrant.
 */
[[nodiscard]] EVENGINE_API_BACKENDS Result<VegetationDetailSettings> restoreVegetationDetails(const Value& document);

}  // namespace eve::graphics
