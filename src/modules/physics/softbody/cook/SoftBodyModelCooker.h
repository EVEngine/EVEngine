#pragma once

#include "common/Result.h"
#include "physics/softbody/SoftBodyModel.h"
#include "physics/softbody/SoftBodyModelDefinition.h"

namespace eve::asset {
struct CanonicalMeshData;
}

namespace eve::physics::softbody_cook {

/**
 * @brief Cook an owning canonical mesh snapshot into a validated soft-body model.
 * @param mesh Borrowed immutable mesh, retained only for this synchronous call.
 * @param definition Validated recipe whose sourceMesh becomes model provenance.
 * @return Complete validated model, or a checked diagnostic; no partial model escapes.
 * @thread Worker-safe and reentrant; performs no IO, callbacks or global mutation.
 */
[[nodiscard("check soft-body model cooking")]]
eve::Result<SoftBodyModel> cookSoftBodyModel(const eve::asset::CanonicalMeshData& mesh,
                                             const SoftBodyModelDefinition&       definition);

}  // namespace eve::physics::softbody_cook
