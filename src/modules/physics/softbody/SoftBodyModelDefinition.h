#pragma once

#include "common/Result.h"
#include "common/Value.h"
#include "schema/SchemaTypes.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace eve::physics {

enum class SoftBodySurfaceSampling { None, Vertices, Voxels };
enum class SoftBodyVolumeSampling { None, Voxels };

/** @brief Versioned authoring recipe used to cook a mesh-backed SoftBodyModel. */
struct SoftBodyModelDefinition {
    static constexpr std::string_view SchemaId      = "physics:softbody-model";
    static constexpr std::uint32_t    SchemaVersion = 1;

    std::string             sourceMesh;
    SoftBodySurfaceSampling surfaceSampling  = SoftBodySurfaceSampling::Vertices;
    int                     surfaceResolution = 16;
    SoftBodyVolumeSampling  volumeSampling   = SoftBodyVolumeSampling::None;
    int                     volumeResolution  = 16;
    int                     shapeResolution   = 48;
    float                   maxAnisotropy     = 3.f;
    float                   smoothing         = 0.25f;

    /** @brief Validate the complete immutable cooking recipe. */
    [[nodiscard("check soft-body model definition")]] eve::Result<void> validate() const;
    /** @brief Encode every version-1 field into an owning persistent value. */
    [[nodiscard("check soft-body model encoding")]] eve::Result<eve::Value> toValue() const;
    /** @brief Decode version 1 transactionally and reject unknown fields. */
    [[nodiscard("check soft-body model decoding")]]
    static eve::Result<SoftBodyModelDefinition> fromValue(const eve::Value& value);
    /** @brief Return the registry schema for tooling and generic JSON validation. */
    [[nodiscard]] static eve::schema::SchemaDefinition schemaDefinition();
    /** @brief Idempotently register version 1 in the process schema registry. */
    [[nodiscard("check soft-body model schema registration")]] static eve::Result<void> ensureSchemaRegistered();
};

}  // namespace eve::physics
