#pragma once
#include "tensor/KernelGenWgsl.h"

// Private implementation shared by the WGSL kernel families.
namespace eve::tensor::wgsl_detail {
inline constexpr int kLocalSize = 256;
std::string          header(int localX, int localY = 1);
std::string          bufferDecl(int binding, const char *name);
std::string          bufferDeclUint(int binding, const char *name);
std::string          pushConstant();
std::string          scalarStr(float value);
std::string          emitQuantizedBVal(DType dtype, int group, const char *bufferName);
int                  groupsFor(int count);
void                 specializeInputBindings(const Graph &, const FusedGroup &, KernelSpec &);
void                 genSoftmax(const Graph &, const FusedGroup &, KernelSpec &);
void                 genNorm(const Graph &, const FusedGroup &, bool rms, KernelSpec &);
void                 genReduceOrArgmax(const Graph &, const FusedGroup &, bool argmax, KernelSpec &);
void                 genEmbedding(const Graph &, const FusedGroup &, KernelSpec &);
void                 genConcat(const Graph &, const FusedGroup &, KernelSpec &);
void                 genSlice(const Graph &, const FusedGroup &, KernelSpec &);
void                 genPermute(const Graph &, const FusedGroup &, KernelSpec &);
void                 genResize2d(const Graph &, const FusedGroup &, KernelSpec &);
void                 genSdpa(const Graph &, const FusedGroup &, KernelSpec &);
}  // namespace eve::tensor::wgsl_detail
