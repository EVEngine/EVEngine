#pragma once
#include "tensor/KernelGenWgsl.h"

// Private implementation shared by the WGSL kernel families.
namespace eve::tensor::wgsl_detail {
inline constexpr int kLocalSize = 256;
/** @brief Header. */
std::string          header(int localX, int localY = 1);
/** @brief Buffer decl. */
std::string          bufferDecl(int binding, const char *name);
/** @brief Buffer decl uint. */
std::string          bufferDeclUint(int binding, const char *name);
/** @brief Pushes constant. */
std::string          pushConstant();
/** @brief Scalar str. */
std::string          scalarStr(float value);
/** @brief Emit quantized b val. */
std::string          emitQuantizedBVal(DType dtype, int group, const char *bufferName);
/** @brief Groups for. */
int                  groupsFor(int count);
/** @brief Specialize input bindings. */
void                 specializeInputBindings(const Graph &, const FusedGroup &, KernelSpec &);
/** @brief Gen softmax. */
void                 genSoftmax(const Graph &, const FusedGroup &, KernelSpec &);
/** @brief Gen norm. */
void                 genNorm(const Graph &, const FusedGroup &, bool rms, KernelSpec &);
/** @brief Gen reduce or argmax. */
void                 genReduceOrArgmax(const Graph &, const FusedGroup &, bool argmax, KernelSpec &);
/** @brief Gen embedding. */
void                 genEmbedding(const Graph &, const FusedGroup &, KernelSpec &);
/** @brief Gen concat. */
void                 genConcat(const Graph &, const FusedGroup &, KernelSpec &);
/** @brief Gen slice. */
void                 genSlice(const Graph &, const FusedGroup &, KernelSpec &);
/** @brief Gen permute. */
void                 genPermute(const Graph &, const FusedGroup &, KernelSpec &);
/** @brief Gen resize 2 d. */
void                 genResize2d(const Graph &, const FusedGroup &, KernelSpec &);
/** @brief Gen sdpa. */
void                 genSdpa(const Graph &, const FusedGroup &, KernelSpec &);
}  // namespace eve::tensor::wgsl_detail
