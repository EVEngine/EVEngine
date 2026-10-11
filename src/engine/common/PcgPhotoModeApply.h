#pragma once
#include "common/Result.h"
#include <cstdint>
#include <string>
#include <variant>

namespace eve {
/** @brief RGBA payload used by the cross-module photo-mode application contract. */
struct PhotoModeColor { float r=0, g=0, b=0, a=1; };
/** @brief Typed payload accepted by authoritative photo-mode domain owners. */
using PhotoModeValue = std::variant<bool, std::int64_t, float, std::string, PhotoModeColor>;
/** @brief Authoritative owner selected for a Pcg photo-mode field. */
enum class PhotoModeDomain { Photo=0, System=1, Graphics=2, Camera=3, Streaming=4, Weather=5, Lighting=6, Water=7, PostFx=8, Terrain=9, Grass=10, Audio=11 };
/** @brief One reversible field assignment in a photo-mode transaction. */
struct PhotoModeAssignment { std::string field; PhotoModeDomain domain=PhotoModeDomain::Photo; PhotoModeValue value; };
/**
 * @brief Capability implemented by the composition root that routes assignments to domain owners.
 *
 * Calls are synchronous on the game thread. Implementations must either apply one assignment fully
 * or leave its domain unchanged. They must not retain references to the assignment or invoke scripts.
 */
/**
 * @brief Optional per-field provider used by the built-in photo-mode capability router.
 *
 * Providers register as capability listeners. Exactly one provider must accept an assignment.
 */
/** @brief Named ownership response from a photo-mode field provider. */
enum class PhotoModeFieldAcceptance { Rejected=0, Accepted=1 };
class IPhotoModeFieldSink {
public:
 static constexpr const char* capabilityName="eve.photo-mode.field";
 virtual ~IPhotoModeFieldSink()=default;
 /** @brief Return true only for fields owned by this provider. */
 virtual PhotoModeFieldAcceptance acceptsPhotoModeField(const PhotoModeAssignment& assignment) const noexcept=0;
 /** @brief Apply one accepted assignment atomically on the game thread. */
 [[nodiscard]] virtual Result<void> applyPhotoModeField(const PhotoModeAssignment& assignment)=0;
};
class IPhotoModeApplySink {
public:
 static constexpr const char* capabilityName="eve.photo-mode.apply";
 virtual ~IPhotoModeApplySink()=default;
 /** @brief Apply one atomic assignment to its authoritative domain. */
 [[nodiscard]] virtual Result<void> applyPhotoModeAssignment(const PhotoModeAssignment& assignment)=0;
};
}
