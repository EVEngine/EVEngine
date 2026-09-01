#pragma once

/** @file IMeshResourceFactory.h @brief Narrow backend boundary for canonical mesh resources. */

#include "common/Result.h"

#include <cstdint>

namespace eve::graphics {

class Mesh;

/** @brief Backend-owned mesh upload and release operations. */
class IMeshResourceFactory {
public:
    virtual ~IMeshResourceFactory() = default;

    /**
     * @brief Upload validated, tightly packed mesh arrays.
     * @return Structured failure or a borrowed backend-owned mesh.
     * @ownership The backend factory owns the returned mesh; caller must release it through this factory.
     * @lifetime Valid until `releaseMesh`, backend shutdown, or graphics-device loss.
     * @thread Must run on the graphics thread. All borrowed input arrays are consumed synchronously.
     */
    [[nodiscard]] virtual Result<Mesh*> uploadMesh(const float* posXYZ, const float* nrmXYZ,
                                                   const float* uvST, int vertexCount,
                                                   const std::uint32_t* indices,
                                                   int indexCount) = 0;

    /**
     * @brief Release a mesh previously returned by this factory.
     * @param mesh Borrowed live mesh owned by this factory; not retained after the call.
     * @thread Must run on the graphics thread.
     */
    [[nodiscard]] virtual Result<void> releaseMesh(Mesh* mesh) = 0;
};

}  // namespace eve::graphics
