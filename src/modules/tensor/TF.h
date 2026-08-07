#pragma once

#include "common/Module.h"

#include <cstdint>
#include <vector>

namespace eve::tensor {

class Tensor;
class Func;
class CompiledFunction;

/**
 * TF2-like namespace module. Script: `tf <- eve.TF();`
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

    Tensor *ones1(int d0);
    Tensor *ones2(int d0, int d1);
    Tensor *ones3(int d0, int d1, int d2);

    Tensor *fill1(int d0, float value);
    Tensor *fill2(int d0, int d1, float value);
    Tensor *fill3(int d0, int d1, int d2, float value);

    Tensor *constantScalar(float value);
    Tensor *arange(int n);
    Tensor *linspace(float start, float end, int n);
    Tensor *eye(int n);

    Tensor *randomUniform1(int d0);
    Tensor *randomUniform2(int d0, int d1);
    Tensor *randomNormal1(int d0);
    Tensor *randomNormal2(int d0, int d1);

    // aliases
    Tensor *rand1(int d0) { return randomUniform1(d0); }
    Tensor *rand2(int d0, int d1) { return randomUniform2(d0, d1); }
    Tensor *randn1(int d0) { return randomNormal1(d0); }
    Tensor *randn2(int d0, int d1) { return randomNormal2(d0, d1); }

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
    Tensor *powScalar(Tensor *a, float exp);
    Tensor *clamp(Tensor *a, float lo, float hi);
    Tensor *maximumScalar(Tensor *a, float s);
    Tensor *minimumScalar(Tensor *a, float s);

    Tensor *matmul(Tensor *a, Tensor *b);
    Tensor *transpose(Tensor *a);
    Tensor *reshape1(Tensor *a, int d0);
    Tensor *reshape2(Tensor *a, int d0, int d1);
    Tensor *reshape3(Tensor *a, int d0, int d1, int d2);
    Tensor *reshape4(Tensor *a, int d0, int d1, int d2, int d3);
    Tensor *flatten(Tensor *a);
    Tensor *where(Tensor *cond, Tensor *a, Tensor *b);

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
