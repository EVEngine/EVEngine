#pragma once

/**
 * @file Mat4Param.h
 * @brief Opaque const-reference carrier for a column-major 4x4 float matrix.
 *
 * Headers that only need to *name* a matrix parameter (virtual Drawable APIs)
 * include this instead of `<glm/mat4x4.hpp>`, which otherwise pulls GLM's SIMD
 * / intrinsics stack into every translation unit. Call sites pass a
 * `glm::mat4` (or any layout-compatible 4x4); implementations recover the
 * concrete type with `as<glm::mat4>()` after including GLM in the .cpp.
 */

namespace eve::graphics {

/**
 * @brief Non-owning view of a 4x4 matrix object.
 * @ownership Borrows the caller's matrix; never retained past the callee return.
 * @lifetime The referenced object must outlive the call that received this view.
 */
class Mat4Param {
public:
    /**
     * @brief Bind any 4x4 matrix object by const reference.
     * @tparam Mat4 Concrete matrix type (typically `glm::mat4`).
     */
    template <class Mat4>
    Mat4Param(const Mat4& matrix) noexcept : ptr_(&matrix) {}

    /**
     * @brief Reinterpret the borrowed object as `Mat4`.
     * @tparam Mat4 Concrete matrix type that was passed at the call site.
     * @return Const reference to the original matrix object.
     */
    template <class Mat4>
    const Mat4& as() const noexcept {
        return *static_cast<const Mat4*>(ptr_);
    }

private:
    const void* ptr_;
};

}  // namespace eve::graphics
