#pragma once

#include "common/Export.h"
#include "common/Value.h"
#include "fluids/FluidSurfaceRenderer.h"
#include "fluids/VolumeFluid.h"
#include "fluids/VolumeFluidDiffuse.h"
#include "fluids/VolumeFluidEmitter.h"
#include "fluids/VolumeFluidFoam.h"

namespace eve::fluids {
/** @brief Projects one complete Fluid3D-renderer settings value. */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeFluidRendererSettings(const FluidRendererSettings& settings);
/** @brief Strictly decodes one complete owning Fluid3D-renderer settings value. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<FluidRendererSettings> decodeFluidRendererSettings(const Value& value);
/** @brief Projects default or caller-prepared Fluid3D-emitter blueprint values. */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeVolumeFluidEmitterBlueprint3D(const VolumeFluidEmitterBlueprint3D& blueprint);
/** @brief Strictly decodes one owning Fluid3D-emitter blueprint value. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeFluidEmitterBlueprint3D> decodeVolumeFluidEmitterBlueprint3D(const Value& value);
/** @brief Projects default or caller-prepared Fluid3D granular-emitter blueprint values. */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeVolumeGranularEmitterBlueprint3D(const VolumeGranularEmitterBlueprint3D& blueprint);
/** @brief Strictly decodes one owning Fluid3D granular-emitter blueprint value. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeGranularEmitterBlueprint3D> decodeVolumeGranularEmitterBlueprint3D(const Value& value);
/** @brief Projects the complete native solver/emission setup produced from a blueprint. */
[[nodiscard]] Value encodeVolumeFluidEmitterBlueprintApplication3D(
    const VolumeFluidEmitterBlueprintApplication3D& application);
/** @brief Projects owning schema-1 foam settings, credit text and RNG state. */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeVolumeFluidFoam(const VolumeFluidFoamSnapshot& snapshot);
/** @brief Strict decode with atomic-controller validation; rejects unknown fields and versions. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeFluidFoamSnapshot> decodeVolumeFluidFoam(const Value& value);
/** @brief Projects the owning version-1 secondary particle pool state. */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeVolumeFluidDiffuse(const VolumeFluidDiffuseSnapshot& snapshot);
/** @brief Strict bounded decode; validates the entire pool before returning owning state. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeFluidDiffuseSnapshot> decodeVolumeFluidDiffuse(const Value& value);
/** @brief Strict transient batch decode of at most 65536 particles; emit validates domains. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidDiffuseParticle>> decodeVolumeFluidDiffuseParticles(const Value& value);
/** @brief Canonical schema-17 value projection, shared by JSON, scripts and authoring. */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeVolumeFluid(const VolumeFluidSnapshot& snapshot);
/** @brief Strict owning decode; unknown/missing fields, unsupported versions and invalid state are rejected. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeFluidSnapshot> decodeVolumeFluid(const Value& value);
/** @brief Encodes a version-9 emission description including precomputed points and particle state. */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeVolumeFluidEmission(const VolumeFluidEmission& emission);
/** @brief Strict structural decode; nozzle/material domain validation occurs atomically at emission. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeFluidEmission> decodeVolumeFluidEmission(const Value& value);
/** @brief Decodes an owning emission batch without serializing the live solver; admission validates domains. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidParticle>> decodeVolumeFluidParticles(const Value& value);
/** @brief Encodes controller state without converting phase text to VM floating point. */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeVolumeFluidEmitterState(const VolumeFluidEmitterSnapshot& state);
/** @brief Strict version-1 controller-state decode; unsupported fields, kinds and phase values fail. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeFluidEmitterSnapshot> decodeVolumeFluidEmitterState(const Value& value);
/** @brief Encodes one version-1 solver/emission/controller checkpoint. */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeVolumeFluidEmitterCheckpoint(const VolumeFluidEmitterCheckpoint& checkpoint);
/** @brief Strictly decodes and validates all owning checkpoint members without publishing state. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<VolumeFluidEmitterCheckpoint> decodeVolumeFluidEmitterCheckpoint(const Value& value);
/** @brief Strict transient pose-argument decode; advanceMoving validates the unit quaternion. */
[[nodiscard]] Result<VolumeFluidNozzlePose> decodeVolumeFluidNozzlePose(const Value& value);
/** @brief Strict transient collider batch decode; setColliders validates geometry atomically. */
[[nodiscard]] Result<std::vector<VolumeFluidCollider>> decodeVolumeFluidColliders(const Value& value);
/** @brief Projects an owning collider description for script construction. */
[[nodiscard]] Value encodeVolumeFluidCollider(const VolumeFluidCollider& collider);
/** @brief Strict owning decode of at most 16 sampled SDF colliders. */
[[nodiscard]] Result<std::vector<VolumeFluidSdfCollider>> decodeVolumeFluidSdfColliders(const Value& value);
/** @brief Projects one complete owning SDF collider. */
[[nodiscard]] Value encodeVolumeFluidSdfCollider(const VolumeFluidSdfCollider& collider);
/** @brief Strict owning decode of at most 16 regular sampled height fields. */
[[nodiscard]] Result<std::vector<VolumeFluidHeightFieldCollider>> decodeVolumeFluidHeightFieldColliders(
    const Value& value);
/** @brief Projects one complete owning regular height field. */
[[nodiscard]] Value encodeVolumeFluidHeightFieldCollider(const VolumeFluidHeightFieldCollider& collider);
/** @brief Strict transient decode of at most 16 SDF transform-only updates. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidSdfPose>> decodeVolumeFluidSdfPoses(const Value& value);
/** @brief Projects one transform-only SDF update for script construction. */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeVolumeFluidSdfPose(const VolumeFluidSdfPose& pose);
/** @brief Projects last-step contacts, including world-space point and opposite impulse. */
[[nodiscard]] Value encodeVolumeFluidContacts(const std::vector<VolumeFluidContact>& contacts);
/** @brief Projects owning Enter/Stay/Exit contact lifecycle events. */
[[nodiscard]] Value encodeVolumeFluidContactEvents(const std::vector<VolumeFluidContactEvent>& events);
/** @brief Projects an owning particle batch without copying the live solver. */
[[nodiscard]] Value encodeVolumeFluidParticles(const std::vector<VolumeFluidParticle>& particles);
/** @brief Projects one owning bounded particle lifecycle-event drain. */
[[nodiscard]] Value encodeVolumeFluidParticleEvents(const VolumeFluidParticleEventBatch& batch);
/** @brief Strict transient material decode; paint validates numeric domains before mutation. */
[[nodiscard]] Result<VolumeFluidMaterial> decodeVolumeFluidMaterial(const Value& value);
/** @brief Strict owning decode of at most 1024 transient thermal rules; step validates domains. */
[[nodiscard]] Result<std::vector<VolumeFluidThermalRule>> decodeVolumeFluidThermalRules(const Value& value);
/** @brief Owning default-rule projection for script construction. */
[[nodiscard]] Value encodeVolumeFluidThermalRule(const VolumeFluidThermalRule& rule);
/** @brief Strict owning decode of at most 32 transient viscosity color keys; application validates domains. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidViscosityColorKey>> decodeVolumeFluidViscosityColors(const Value& value);
/** @brief Strict owning decode of a 2..32-key normalized linear RGBA gradient. */
[[nodiscard]] Result<std::vector<VolumeFluidColorKey>> decodeVolumeFluidColorGradient(const Value& value);
/** @brief Strict owning decode of up to 65536 transient world-space field query positions. */
[[nodiscard]] Result<std::vector<glm::vec3>> decodeVolumeFluidFieldPositions(const Value& value);
/** @brief Strict owning decode of at most 64 transient Fluid3D-style wind zones. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidWindZone>> decodeVolumeFluidWindZones(const Value& value);
/** @brief Projects one complete wind-zone description for script construction. */
[[nodiscard]] EVENGINE_API_DOMAINS Value encodeVolumeFluidWindZone(const VolumeFluidWindZone& zone);
/** @brief Projects owning field samples, preserving query order. */
[[nodiscard]] Value encodeVolumeFluidFieldSamples(const std::vector<VolumeFluidFieldSample>& samples);
/** @brief Projects owning ray hits including captured particle values. */
[[nodiscard]] Value encodeVolumeFluidRayHits(const std::vector<VolumeFluidRayHit>& hits);
/** @brief Projects owning signed-distance hits including captured particle values. */
[[nodiscard]] Value encodeVolumeFluidDistanceHits(const std::vector<VolumeFluidDistanceHit>& hits);
/** @brief Strict owning decode of at most 256 transient mixed query descriptions. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<VolumeFluidQueryShape>> decodeVolumeFluidQueries(const Value& value);
/** @brief Strict owning decode of at most 256 transient linear RGBA colors. */
[[nodiscard]] EVENGINE_API_DOMAINS Result<std::vector<glm::vec4>> decodeVolumeFluidColors(const Value& value);
/** @brief Projects owning mixed-query hits with their source query indices. */
[[nodiscard]] Value encodeVolumeFluidQueryHits(const std::vector<VolumeFluidQueryHit>& hits);
/** @brief Strict owning decode of at most 65536 point, edge or triangle topology entries. */
[[nodiscard]] Result<std::vector<VolumeFluidSimplex>> decodeVolumeFluidSimplexes(const Value& value);
/** @brief Strict owning decode of at most 65536 Fluid3DStitcher particle pairs. */
[[nodiscard]] Result<std::vector<VolumeFluidStitch>> decodeVolumeFluidStitches(const Value& value);
/** @brief Projects owning Fluid3D-compatible simplex query results. */
[[nodiscard]] Value encodeVolumeFluidSimplexHits(const std::vector<VolumeFluidSimplexHit>& hits);
}  // namespace eve::fluids
