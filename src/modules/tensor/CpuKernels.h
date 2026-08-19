#ifndef EVE_TENSOR_CPUKERNELS_H
#define EVE_TENSOR_CPUKERNELS_H

#include "tensor/Graph.h"

#include <cstddef>

namespace eve::tensor {

/**
 * CPU reference kernels shared by the eager path and the graph interpreter.
 *
 * All shapes are static, row-major, rank <= Tensor::kMaxRank. Every entry
 * point is a plain loop implementation: correctness / portability first,
 * throughput is provided by the generated GPU kernels when a Vulkan device
 * is available.
 */
namespace kernels {

/** Apply a scalar unary op (elementwise) to one value. */
float applyUnary(OpType type, float x, float s0, float s1);

/** Whether the op is a fusible elementwise op (also used by the optimizer). */
bool isElementwiseOp(OpType type);

/** Broadcast two shapes to a common shape. Returns false when incompatible. */
bool broadcastShape(const int *aDims, int aRank, const int *bDims, int bRank, int *outDims,
                    int &outRank);

/** Elementwise binary op with broadcasting. */
void binaryOp(OpType type, const float *a, const int *aDims, int aRank, const float *b,
              const int *bDims, int bRank, float *out, const int *outDims, int outRank);

/** Elementwise unary / scalar op. */
void unaryOp(OpType type, const float *in, int count, float *out, float s0, float s1);

void softmax(const float *in, const int *dims, int rank, int axis, bool logMode, float *out);
void layernorm(const float *in, int rows, int cols, const float *scale, const float *bias,
               float eps, float *out);
void rmsnorm(const float *in, int rows, int cols, const float *scale, float eps, float *out);

void conv1d(const float *x, const int *xDims, const float *w, const int *wDims,
            const float *bias, int stride, int pad, float *out);
void conv2d(const float *x, const int *xDims, const float *w, const int *wDims,
            const float *bias, int stride, int pad, float *out);
void maxpool2d(const float *in, const int *dims, int ksize, int stride, int pad, float *out);
void avgpool2d(const float *in, const int *dims, int ksize, int stride, int pad, float *out);

/** table [vocab x dim], indices [count] -> out [count x dim]. Indices are clamped. */
void embedding(const float *table, int vocab, int dim, const float *indices, int count,
               float *out);

/** concat n (2..4) tensors along axis; all other dims must match out dims. */
void concat(const float *const *ins, const int *const *inDims, const int *inRanks, int n,
            int axis, float *out, const int *outDims, int outRank);

void sliceOp(const float *in, const int *inDims, int inRank, int axis, int begin, int end,
             float *out, const int *outDims, int outRank);
void permute(const float *in, const int *inDims, int rank, const int *order, float *out,
             const int *outDims);

/** sum/mean/min/max along axis (out dims must already be computed by caller). */
void reduceAxis(OpType type, const float *in, const int *dims, int rank, int axis, float *out,
                const int *outDims, int outRank);
void argmax(const float *in, const int *dims, int rank, int axis, float *out,
            const int *outDims, int outRank);

/**
 * Scaled dot-product attention over the last two dims:
 * q [B,H,T,D], k [B,H,S,D], v [B,H,S,D], mask [B,H,T,S] (optional, may be null).
 */
void sdpa(const float *q, const float *k, const float *v, const float *mask, int B, int H,
          int T, int S, int D, float scale, float *out);

/** resize2d NCHW; mode: 0 = nearest, 1 = bilinear. */
void resize2d(const float *in, const int *inDims, int outW, int outH, int mode, float *out);

}  // namespace kernels
}  // namespace eve::tensor

#endif  // EVE_TENSOR_CPUKERNELS_H
