#pragma once
#include "common/Export.h"


#include "common/Module.h"

#include <cstdint>
#include <vector>

namespace eve::tensor {

class Tensor;
class Func;
class CompiledFunction;

/**
 * @brief TF2-like namespace module. Script: `tf <- eve.TF();`
 * Default eager; `tf.func()` traces a graph for compile/run.
 */
class EVENGINE_API_DOMAINS TF : public Module {
public:
    Module_REG(TF);
    /** @brief Tf. */
    TF();
    /** @brief Tf. */
    ~TF() override = default;

    /** @brief Func. */
    Func *func();

    /** @brief Pushes trace. */
    void pushTrace(Func *f);
    /** @brief Pops trace. */
    void popTrace(Func *f);
    /** @brief Tracing. */
    Func *tracing() const;

    // --- factories (eager, or Const nodes while tracing) ---
    /** @brief Zeros 1. */
    Tensor *zeros1(int d0);
    /** @brief Zeros 2. */
    Tensor *zeros2(int d0, int d1);
    /** @brief Zeros 3. */
    Tensor *zeros3(int d0, int d1, int d2);
    /** @brief Zeros 4. */
    Tensor *zeros4(int d0, int d1, int d2, int d3);
    /** @brief Zeros 5. */
    Tensor *zeros5(int d0, int d1, int d2, int d3, int d4);
    /** @brief Zeros 6. */
    Tensor *zeros6(int d0, int d1, int d2, int d3, int d4, int d5);

    /** @brief Ones 1. */
    Tensor *ones1(int d0);
    /** @brief Ones 2. */
    Tensor *ones2(int d0, int d1);
    /** @brief Ones 3. */
    Tensor *ones3(int d0, int d1, int d2);
    /** @brief Ones 4. */
    Tensor *ones4(int d0, int d1, int d2, int d3);
    /** @brief Ones 5. */
    Tensor *ones5(int d0, int d1, int d2, int d3, int d4);
    /** @brief Ones 6. */
    Tensor *ones6(int d0, int d1, int d2, int d3, int d4, int d5);

    /** @brief Fill 1. */
    Tensor *fill1(int d0, float value);
    /** @brief Fill 2. */
    Tensor *fill2(int d0, int d1, float value);
    /** @brief Fill 3. */
    Tensor *fill3(int d0, int d1, int d2, float value);
    /** @brief Fill 4. */
    Tensor *fill4(int d0, int d1, int d2, int d3, float value);

    /** @brief Constant scalar. */
    Tensor *constantScalar(float value);
    /** @brief Arange. */
    Tensor *arange(int n);
    /** @brief Linspace. */
    Tensor *linspace(float start, float end, int n);
    /** @brief Eye. */
    Tensor *eye(int n);

    /** @brief Random uniform 1. */
    Tensor *randomUniform1(int d0);
    /** @brief Random uniform 2. */
    Tensor *randomUniform2(int d0, int d1);
    /** @brief Random uniform 3. */
    Tensor *randomUniform3(int d0, int d1, int d2);
    /** @brief Random uniform 4. */
    Tensor *randomUniform4(int d0, int d1, int d2, int d3);
    /** @brief Random normal 1. */
    Tensor *randomNormal1(int d0);
    /** @brief Random normal 2. */
    Tensor *randomNormal2(int d0, int d1);
    /** @brief Random normal 3. */
    Tensor *randomNormal3(int d0, int d1, int d2);
    /** @brief Random normal 4. */
    Tensor *randomNormal4(int d0, int d1, int d2, int d3);

    // aliases
    /** @brief Rand 1. */
    Tensor *rand1(int d0) { return randomUniform1(d0); }
    /** @brief Rand 2. */
    Tensor *rand2(int d0, int d1) { return randomUniform2(d0, d1); }
    /** @brief Rand 3. */
    Tensor *rand3(int d0, int d1, int d2) { return randomUniform3(d0, d1, d2); }
    /** @brief Rand 4. */
    Tensor *rand4(int d0, int d1, int d2, int d3) {
        /** @brief Random uniform 4. */
        return randomUniform4(d0, d1, d2, d3);
    }
    /** @brief Randn 1. */
    Tensor *randn1(int d0) { return randomNormal1(d0); }
    /** @brief Randn 2. */
    Tensor *randn2(int d0, int d1) { return randomNormal2(d0, d1); }
    /** @brief Randn 3. */
    Tensor *randn3(int d0, int d1, int d2) { return randomNormal3(d0, d1, d2); }
    /** @brief Randn 4. */
    Tensor *randn4(int d0, int d1, int d2, int d3) {
        /** @brief Random normal 4. */
        return randomNormal4(d0, d1, d2, d3);
    }

    /** @brief Sets the random seed. */
    void     setRandomSeed(uint32_t seed);
    /** @brief Returns the random seed. */
    uint32_t getRandomSeed() const;

    // --- module-level ops (TF style) ---
    /** @brief Adds add. */
    Tensor *add(Tensor *a, Tensor *b);
    /** @brief Sub. */
    Tensor *sub(Tensor *a, Tensor *b);
    /** @brief Multiply. */
    Tensor *multiply(Tensor *a, Tensor *b);
    /** @brief Div. */
    Tensor *div(Tensor *a, Tensor *b);
    /** @brief Adds scalar. */
    Tensor *addScalar(Tensor *a, float s);
    /** @brief Sub scalar. */
    Tensor *subScalar(Tensor *a, float s);
    /** @brief Mul scalar. */
    Tensor *mulScalar(Tensor *a, float s);
    /** @brief Div scalar. */
    Tensor *divScalar(Tensor *a, float s);
    /** @brief Neg. */
    Tensor *neg(Tensor *a);
    /** @brief Abs. */
    Tensor *abs(Tensor *a);
    /** @brief Sqrt. */
    Tensor *sqrt(Tensor *a);
    /** @brief Exp. */
    Tensor *exp(Tensor *a);
    /** @brief Log. */
    Tensor *log(Tensor *a);
    /** @brief Sin. */
    Tensor *sin(Tensor *a);
    /** @brief Cos. */
    Tensor *cos(Tensor *a);
    /** @brief Tanh. */
    Tensor *tanh(Tensor *a);
    /** @brief Relu. */
    Tensor *relu(Tensor *a);
    /** @brief Sigmoid. */
    Tensor *sigmoid(Tensor *a);
    /** @brief Gelu. */
    Tensor *gelu(Tensor *a);
    /** @brief Silu. */
    Tensor *silu(Tensor *a);
    /** @brief Pow scalar. */
    Tensor *powScalar(Tensor *a, float exp);
    /** @brief Clamp. */
    Tensor *clamp(Tensor *a, float lo, float hi);
    /** @brief Maximum scalar. */
    Tensor *maximumScalar(Tensor *a, float s);
    /** @brief Minimum scalar. */
    Tensor *minimumScalar(Tensor *a, float s);

    /** @brief Matmul. */
    Tensor *matmul(Tensor *a, Tensor *b);
    /** @brief Transpose. */
    Tensor *transpose(Tensor *a);
    /** @brief Permute 2. */
    Tensor *permute2(Tensor *a, int a0, int a1);
    /** @brief Permute 3. */
    Tensor *permute3(Tensor *a, int a0, int a1, int a2);
    /** @brief Permute 4. */
    Tensor *permute4(Tensor *a, int a0, int a1, int a2, int a3);
    /** @brief Permute 5. */
    Tensor *permute5(Tensor *a, int a0, int a1, int a2, int a3, int a4);
    /** @brief Permute 6. */
    Tensor *permute6(Tensor *a, int a0, int a1, int a2, int a3, int a4, int a5);
    /** @brief Reshape 1. */
    Tensor *reshape1(Tensor *a, int d0);
    /** @brief Reshape 2. */
    Tensor *reshape2(Tensor *a, int d0, int d1);
    /** @brief Reshape 3. */
    Tensor *reshape3(Tensor *a, int d0, int d1, int d2);
    /** @brief Reshape 4. */
    Tensor *reshape4(Tensor *a, int d0, int d1, int d2, int d3);
    /** @brief Reshape 5. */
    Tensor *reshape5(Tensor *a, int d0, int d1, int d2, int d3, int d4);
    /** @brief Reshape 6. */
    Tensor *reshape6(Tensor *a, int d0, int d1, int d2, int d3, int d4, int d5);
    /** @brief Flatten. */
    Tensor *flatten(Tensor *a);
    /** @brief Where. */
    Tensor *where(Tensor *cond, Tensor *a, Tensor *b);
    /** @brief Concat n. */
    Tensor *concatN(Tensor *const *ins, int n, int axis);

    // --- neural / speech / terrain ops (eager + traceable) ---
    /** @brief Softmax. */
    Tensor *softmax(Tensor *a, int axis);
    /** @brief Log softmax. */
    Tensor *logSoftmax(Tensor *a, int axis);
    /** @brief Layernorm. */
    Tensor *layernorm(Tensor *a, float eps);
    /** @brief Layernorm wb. */
    Tensor *layernormWB(Tensor *a, Tensor *scale, Tensor *bias, float eps);
    /** @brief Rmsnorm. */
    Tensor *rmsnorm(Tensor *a, float eps);
    /** @brief Rmsnorm w. */
    Tensor *rmsnormW(Tensor *a, Tensor *scale, float eps);
    /** @brief Conv 1 d. */
    Tensor *conv1d(Tensor *x, Tensor *w, int stride, int pad);
    /** @brief Conv 1 d bias. */
    Tensor *conv1dBias(Tensor *x, Tensor *w, Tensor *bias, int stride, int pad);
    /** @brief Conv 2 d. */
    Tensor *conv2d(Tensor *x, Tensor *w, int stride, int pad);
    /** @brief Conv 2 d bias. */
    Tensor *conv2dBias(Tensor *x, Tensor *w, Tensor *bias, int stride, int pad);
    /** @brief Maxpool 2 d. */
    Tensor *maxpool2d(Tensor *x, int ksize, int stride, int pad);
    /** @brief Avgpool 2 d. */
    Tensor *avgpool2d(Tensor *x, int ksize, int stride, int pad);
    /** @brief Embedding. */
    Tensor *embedding(Tensor *table, Tensor *indices);
    /** @brief Concat 2. */
    Tensor *concat2(Tensor *a, Tensor *b, int axis);
    /** @brief Concat 3. */
    Tensor *concat3(Tensor *a, Tensor *b, Tensor *c, int axis);
    /** @brief Concat 4. */
    Tensor *concat4(Tensor *a, Tensor *b, Tensor *c, Tensor *d, int axis);
    /** @brief Slice. */
    Tensor *slice(Tensor *a, int axis, int begin, int end);
    /** @brief Sum axis. */
    Tensor *sumAxis(Tensor *a, int axis, int keepDims);
    /** @brief Mean axis. */
    Tensor *meanAxis(Tensor *a, int axis, int keepDims);
    /** @brief Min axis. */
    Tensor *minAxis(Tensor *a, int axis, int keepDims);
    /** @brief Max axis. */
    Tensor *maxAxis(Tensor *a, int axis, int keepDims);
    /** @brief Argmax. */
    Tensor *argmax(Tensor *a, int axis, int keepDims);
    /** @brief Cast. */
    Tensor *cast(Tensor *a, const std::string &dtype);
    /** @brief Sdpa. */
    Tensor *sdpa(Tensor *q, Tensor *k, Tensor *v, float scale);
    /** @brief Sdpa masked. */
    Tensor *sdpaMasked(Tensor *q, Tensor *k, Tensor *v, Tensor *mask, float scale);
    /** @brief Resize 2 d. */
    Tensor *resize2d(Tensor *a, int outW, int outH, int mode);

    /**
     * Weight-only quantization (eager): pack a float32 tensor into one of the
     * packed weight dtypes — "fp16", "fp8", "fp4", "int8", "int4". int8/int4
     * use symmetric per-group scales (group = elements per scale, default all).
     */
    /** @brief Quantize weight. */
    Tensor *quantizeWeight(Tensor *a, const std::string &dtype, int group = 0);

    /** @brief Reduce sum. */
    float reduceSum(Tensor *a);
    /** @brief Reduce mean. */
    float reduceMean(Tensor *a);
    /** @brief Reduce min. */
    float reduceMin(Tensor *a);
    /** @brief Reduce max. */
    float reduceMax(Tensor *a);

private:
    uint32_t              seed_     = 1;
    mutable uint32_t      rngState_ = 1;
    std::vector<Func *>   traceStack_;

    float   nextUniform() const;
    float   nextGaussian() const;
    Tensor *filled(const int *dims, int rank, float value);
};

}  // namespace eve::tensor
