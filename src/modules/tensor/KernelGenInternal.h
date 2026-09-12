#pragma once
#include "tensor/KernelGen.h"

// Private GLSL emission helpers shared by the kernel families.
namespace eve::tensor::glsl_detail {
inline constexpr int kLocalSize = 256;
std::string          header(int localX, int localY = 1);
std::string          bufferDecl(int binding, const char *name);
std::string          pushConstant();
std::string          scalarStr(float value);
int                  groupsFor(int count);
void                 genResize2d(const Graph &, const FusedGroup &, KernelSpec &);
}  // namespace eve::tensor::glsl_detail
