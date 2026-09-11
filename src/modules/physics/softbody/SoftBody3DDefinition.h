#pragma once

#include "common/Result.h"
#include "common/Value.h"
#include "schema/SchemaTypes.h"

#include <cstdint>
#include <string_view>

namespace eve::physics {

/**
 * @brief Versioned, owning creation definition for one volumetric soft body.
 *
 * This is the canonical source for runtime creation, serialization and editor
 * defaults. Version 1 rejects unknown fields and unsupported versions; there
 * is no older released version requiring migration.
 */
struct SoftBody3DDefinition {
    static constexpr std::string_view SchemaId      = "physics:softbody3d";
    static constexpr std::uint32_t    SchemaVersion = 1;

    int   cols = 4, rows = 4, layers = 4;
    float spacing = 0.35f;
    float originX = 0.f, originY = 0.f, originZ = 0.f;
    float gravityX = 0.f, gravityY = -9.8f, gravityZ = 0.f;
    float deformationResistance = 0.8f;
    int   iterations            = 5;
    float damping = 0.04f, particleRadius = 0.2f, particleMass = 0.1f;
    float plasticYield = 0.f, plasticCreep = 0.f, plasticRecovery = 0.f, maxDeformation = 0.f;
    bool  selfCollision = false;

    /** @brief Validate all fields and the aggregate one-million-particle limit. */
    [[nodiscard("check soft-body definition validation")]] eve::Result<void> validate() const;
    /** @brief Encode the complete canonical version-1 value. */
    [[nodiscard("check soft-body definition encoding")]] eve::Result<eve::Value> toValue() const;
    /** @brief Build the runtime/tooling schema whose constraints mirror validate(). */
    [[nodiscard]] static eve::schema::SchemaDefinition schemaDefinition();
    /** @brief Idempotently register version 1 in the process schema registry. */
    [[nodiscard("check soft-body schema registration")]] static eve::Result<void> ensureSchemaRegistered();
    /** @brief Decode and validate version 1 transactionally without partial publication. */
    [[nodiscard("check soft-body definition decoding")]]
    static eve::Result<SoftBody3DDefinition> fromValue(const eve::Value& value);
};

}  // namespace eve::physics
