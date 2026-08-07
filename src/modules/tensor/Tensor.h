#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eve::tensor {

class Graph;
class Func;
class TF;

/**
 * float32 tensor (rank 1–4), row-major.
 * Eager: owns a buffer. Symbolic: node in a Func graph (no buffer until run).
 */
class Tensor {
public:
    static constexpr int kMaxRank = 4;

    Tensor() = default;
    explicit Tensor(const int *dims, int rank);
    Tensor(int d0);
    Tensor(int d0, int d1);
    Tensor(int d0, int d1, int d2);
    Tensor(int d0, int d1, int d2, int d3);

    /** Symbolic handle into a graph node. */
    static Tensor *makeSymbolic(Graph *graph, int nodeId, const int *dims, int rank);

    bool        isSymbolic() const { return kind_ == Kind::Symbolic; }
    bool        isEager() const { return kind_ == Kind::Eager; }
    Graph      *graph() const { return graph_; }
    int         nodeId() const { return nodeId_; }

    int         getRank() const { return rank_; }
    int         getSize() const { return size_; }
    int         getDim(int axis) const;
    int         getDim0() const { return dims_[0]; }
    int         getDim1() const { return dims_[1]; }
    int         getDim2() const { return dims_[2]; }
    int         getDim3() const { return dims_[3]; }
    std::string getDevice() const { return device_; }
    std::string getDtype() const { return "float32"; }

    float get(int flatIndex) const;
    void  set(int flatIndex, float value);
    float get1(int i0) const;
    void  set1(int i0, float value);
    float get2(int i0, int i1) const;
    void  set2(int i0, int i1, float value);
    float get3(int i0, int i1, int i2) const;
    void  set3(int i0, int i1, int i2, float value);
    float get4(int i0, int i1, int i2, int i3) const;
    void  set4(int i0, int i1, int i2, int i3, float value);

    void fill(float value);
    void copyFrom(const Tensor *other);
    Tensor *clone() const;

    // Eager instance sugar (throws if symbolic)
    Tensor *add(const Tensor *other) const;
    Tensor *sub(const Tensor *other) const;
    Tensor *multiply(const Tensor *other) const;
    Tensor *div(const Tensor *other) const;
    Tensor *addScalar(float s) const;
    Tensor *subScalar(float s) const;
    Tensor *mulScalar(float s) const;
    Tensor *divScalar(float s) const;
    Tensor *neg() const;
    Tensor *abs() const;
    Tensor *sqrt() const;
    Tensor *exp() const;
    Tensor *log() const;
    Tensor *sin() const;
    Tensor *cos() const;
    Tensor *tanh() const;
    Tensor *relu() const;
    Tensor *sigmoid() const;
    Tensor *powScalar(float exp) const;
    Tensor *clamp(float lo, float hi) const;
    Tensor *maximumScalar(float s) const;
    Tensor *minimumScalar(float s) const;

    void addInPlace(const Tensor *other);
    void multiplyInPlace(const Tensor *other);
    void addScalarInPlace(float s);
    void mulScalarInPlace(float s);
    void reluInPlace();

    float reduceSum() const;
    float reduceMean() const;
    float reduceMin() const;
    float reduceMax() const;
    float dot(const Tensor *other) const;

    Tensor *matmul(const Tensor *other) const;
    Tensor *transpose() const;
    Tensor *reshape1(int d0) const;
    Tensor *reshape2(int d0, int d1) const;
    Tensor *reshape3(int d0, int d1, int d2) const;
    Tensor *reshape4(int d0, int d1, int d2, int d3) const;
    Tensor *flatten() const;

    float       *data();
    const float *data() const;

    void ensureEager(const char *op) const;

private:
    friend class TF;
    friend class Func;
    friend class CompiledFunction;
    friend class Graph;

    enum class Kind { Eager, Symbolic };

    void initDims(const int *dims, int rank);
    void checkSameShape(const Tensor *other, const char *op) const;
    int  offset2(int i0, int i1) const;
    int  offset3(int i0, int i1, int i2) const;
    int  offset4(int i0, int i1, int i2, int i3) const;

    Kind              kind_   = Kind::Eager;
    Graph            *graph_  = nullptr;
    int               nodeId_ = -1;
    int               rank_   = 0;
    int               dims_[kMaxRank] = {0, 0, 0, 0};
    int               size_   = 0;
    std::vector<float> data_;
    std::string       device_ = "cpu";
};

}  // namespace eve::tensor
