#pragma once
#include "tensor/KernelGen.h"

// Private GLSL emission helpers shared by the kernel families.
namespace eve::tensor::glsl_detail {
inline constexpr int kLocalSize = 256;
/** @brief Header. */
std::string          header(int localX, int localY = 1);
/** @brief Buffer decl. */
std::string          bufferDecl(int binding, const char *name);
/** @brief Pushes constant. */
std::string          pushConstant();
/** @brief Scalar str. */
std::string          scalarStr(float value);
/** @brief Groups for. */
int                  groupsFor(int count);
/** @brief Gen resize 2 d. */
void                 genResize2d(const Graph &, const FusedGroup &, KernelSpec &);
}  // namespace eve::tensor::glsl_detail
