#pragma once

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
class TF : public Module {
public:
    Module_REG(TF);
    TF();
    ~TF() override = default;

    Func *func();

    void pushTrace(Func *f);
    void popTrace(Func *f);
    Func *tracing() const;

    // --- factories (eager, or Const nodes while tracing) ---
    Tensor *zeros1(int d0);
    Tensor *zeros2(int d0, int d1);
    Tensor *zeros3(int d0, int d1, int d2);
    Tensor *zeros4(int d0, int d1, int d2, int d3);
    Tensor *zeros5(int d0, int d1, int d2, int d3, int d4);
    Tensor *zeros6(int d0, int d1, int d2, int d3, int d4, int d5);

    Tensor *ones1(int d0);
    Tensor *ones2(int d0, int d1);
    Tensor *ones3(int d0, int d1, int d2);
    Tensor *ones4(int d0, int d1, int d2, int d3);
    Tensor *ones5(int d0, int d1, int d2, int d3, int d4);
    Tensor *ones6(int d0, int d1, int d2, int d3, int d4, int d5);

    Tensor *fill1(int d0, float value);
    Tensor *fill2(int d0, int d1, float value);
    Tensor *fill3(int d0, int d1, int d2, float value);
    Tensor *fill4(int d0, int d1, int d2, int d3, float value);

    Tensor *constantScalar(float value);
    Tensor *arange(int n);
    Tensor *linspace(float start, float end, int n);
    Tensor *eye(int n);

    Tensor *randomUniform1(int d0);
    Tensor *randomUniform2(int d0, int d1);
    Tensor *randomUniform3(int d0, int d1, int d2);
    Tensor *randomUniform4(int d0, int d1, int d2, int d3);
    Tensor *randomNormal1(int d0);
    Tensor *randomNormal2(int d0, int d1);
    Tensor *randomNormal3(int d0, int d1, int d2);
    Tensor *randomNormal4(int d0, int d1, int d2, int d3);

    // aliases
    Tensor *rand1(int d0) { return randomUniform1(d0); }
    Tensor *rand2(int d0, int d1) { return randomUniform2(d0, d1); }
    Tensor *rand3(int d0, int d1, int d2) { return randomUniform3(d0, d1, d2); }
    Tensor *rand4(int d0, int d1, int d2, int d3) {
        return randomUniform4(d0, d1, d2, d3);
    }
    Tensor *randn1(int d0) { return randomNormal1(d0); }
    Tensor *randn2(int d0, int d1) { return randomNormal2(d0, d1); }
    Tensor *randn3(int d0, int d1, int d2) { return randomNormal3(d0, d1, d2); }
    Tensor *randn4(int d0, int d1, int d2, int d3) {
        return randomNormal4(d0, d1, d2, d3);
    }

    void     setRandomSeed(uint32_t seed);
    uint32_t getRandomSeed() const;

    // --- module-level ops (TF style) ---
    Tensor *add(Tensor *a, Tensor *b);
    Tensor *sub(Tensor *a, Tensor *b);
    Tensor *multiply(Tensor *a, Tensor *b);
    Tensor *div(Tensor *a, Tensor *b);
    Tensor *addScalar(Tensor *a, float s);
    Tensor *subScalar(Tensor *a, float s);
    Tensor *mulScalar(Tensor *a, float s);
    Tensor *divScalar(Tensor *a, float s);
    Tensor *neg(Tensor *a);
    Tensor *abs(Tensor *a);
    Tensor *sqrt(Tensor *a);
    Tensor *exp(Tensor *a);
    Tensor *log(Tensor *a);
    Tensor *sin(Tensor *a);
    Tensor *cos(Tensor *a);
    Tensor *tanh(Tensor *a);
    Tensor *relu(Tensor *a);
    Tensor *sigmoid(Tensor *a);
    Tensor *gelu(Tensor *a);
    Tensor *silu(Tensor *a);
    Tensor *powScalar(Tensor *a, float exp);
    Tensor *clamp(Tensor *a, float lo, float hi);
    Tensor *maximumScalar(Tensor *a, float s);
    Tensor *minimumScalar(Tensor *a, float s);

    Tensor *matmul(Tensor *a, Tensor *b);
    Tensor *transpose(Tensor *a);
    Tensor *permute2(Tensor *a, int a0, int a1);
    Tensor *permute3(Tensor *a, int a0, int a1, int a2);
    Tensor *permute4(Tensor *a, int a0, int a1, int a2, int a3);
    Tensor *permute5(Tensor *a, int a0, int a1, int a2, int a3, int a4);
    Tensor *permute6(Tensor *a, int a0, int a1, int a2, int a3, int a4, int a5);
    Tensor *reshape1(Tensor *a, int d0);
    Tensor *reshape2(Tensor *a, int d0, int d1);
    Tensor *reshape3(Tensor *a, int d0, int d1, int d2);
    Tensor *reshape4(Tensor *a, int d0, int d1, int d2, int d3);
    Tensor *reshape5(Tensor *a, int d0, int d1, int d2, int d3, int d4);
    Tensor *reshape6(Tensor *a, int d0, int d1, int d2, int d3, int d4, int d5);
    Tensor *flatten(Tensor *a);
    Tensor *where(Tensor *cond, Tensor *a, Tensor *b);
    Tensor *concatN(Tensor *const *ins, int n, int axis);

    // --- neural / speech / terrain ops (eager + traceable) ---
    Tensor *softmax(Tensor *a, int axis);
    Tensor *logSoftmax(Tensor *a, int axis);
    Tensor *layernorm(Tensor *a, float eps);
    Tensor *layernormWB(Tensor *a, Tensor *scale, Tensor *bias, float eps);
    Tensor *rmsnorm(Tensor *a, float eps);
    Tensor *rmsnormW(Tensor *a, Tensor *scale, float eps);
    Tensor *conv1d(Tensor *x, Tensor *w, int stride, int pad);
    Tensor *conv1dBias(Tensor *x, Tensor *w, Tensor *bias, int stride, int pad);
    Tensor *conv2d(Tensor *x, Tensor *w, int stride, int pad);
    Tensor *conv2dBias(Tensor *x, Tensor *w, Tensor *bias, int stride, int pad);
    Tensor *maxpool2d(Tensor *x, int ksize, int stride, int pad);
    Tensor *avgpool2d(Tensor *x, int ksize, int stride, int pad);
    Tensor *embedding(Tensor *table, Tensor *indices);
    Tensor *concat2(Tensor *a, Tensor *b, int axis);
    Tensor *concat3(Tensor *a, Tensor *b, Tensor *c, int axis);
    Tensor *concat4(Tensor *a, Tensor *b, Tensor *c, Tensor *d, int axis);
    Tensor *slice(Tensor *a, int axis, int begin, int end);
    Tensor *sumAxis(Tensor *a, int axis, int keepDims);
    Tensor *meanAxis(Tensor *a, int axis, int keepDims);
    Tensor *minAxis(Tensor *a, int axis, int keepDims);
    Tensor *maxAxis(Tensor *a, int axis, int keepDims);
    Tensor *argmax(Tensor *a, int axis, int keepDims);
    Tensor *cast(Tensor *a, const std::string &dtype);
    Tensor *sdpa(Tensor *q, Tensor *k, Tensor *v, float scale);
    Tensor *sdpaMasked(Tensor *q, Tensor *k, Tensor *v, Tensor *mask, float scale);
    Tensor *resize2d(Tensor *a, int outW, int outH, int mode);

    /**
     * Weight-only quantization (eager): pack a float32 tensor into one of the
     * packed weight dtypes — "fp16", "fp8", "fp4", "int8", "int4". int8/int4
     * use symmetric per-group scales (group = elements per scale, default all).
     */
    Tensor *quantizeWeight(Tensor *a, const std::string &dtype, int group = 0);

    float reduceSum(Tensor *a);
    float reduceMean(Tensor *a);
    float reduceMin(Tensor *a);
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
