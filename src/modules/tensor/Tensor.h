#ifndef EVE_TENSOR_TENSOR_H
#define EVE_TENSOR_TENSOR_H

#include <cstdint>
#include <string>
#include <vector>

namespace eve::tensor {

class Graph;
class Func;
class TF;

/**
 * Tensor element types.
 *
 * Script-visible tensors are float32; int32 tensors are used for index data
 * (argmax outputs, embedding lookups, cast("int32")). Int32 values are stored
 * losslessly as floats for |v| < 2^24, which comfortably covers model
 * vocabularies / sequence lengths / simulation ids used in games.
 */
enum class DType : uint8_t {
    Float32 = 0,
    Int32   = 1,
    Fp16    = 2,  // IEEE half, 2 bytes/elem, weight-only
    Fp8E4M3 = 3,  // 1 byte/elem, weight-only
    Fp4E2M1 = 4,  // 4-bit e2m1, two per byte, weight-only
    Int8    = 5,  // 1 byte/elem + per-group scale, weight-only
    Int4    = 6,  // 4-bit + per-group scale, weight-only
};

namespace q {
bool isQuantDType(DType dt);
}

const char *dtypeName(DType dtype);
bool parseDType(const std::string &name, DType &out);

/**
 * float32 / int32 tensor (rank 1–6), row-major.
 * Eager: owns a buffer. Symbolic: node in a Func graph (no buffer until run).
 */
class Tensor {
public:
    static constexpr int kMaxRank = 6;

    Tensor() = default;
    explicit Tensor(const int *dims, int rank);
    explicit Tensor(DType dtype, const int *dims, int rank);
    Tensor(int d0);
    Tensor(int d0, int d1);
    Tensor(int d0, int d1, int d2);
    Tensor(int d0, int d1, int d2, int d3);
    Tensor(int d0, int d1, int d2, int d3, int d4);
    Tensor(int d0, int d1, int d2, int d3, int d4, int d5);

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
    int         getDim4() const { return dims_[4]; }
    int         getDim5() const { return dims_[5]; }
    std::string getDevice() const { return device_; }
    std::string getDtype() const { return dtypeName(dtype_); }
    DType       dtype() const { return dtype_; }
    void        setDtype(DType dtype) { dtype_ = dtype; }

    /** True for packed weight-quantization dtypes (bytes_, qScales_). */
    bool isQuantized() const { return q::isQuantDType(dtype_); }

    /** Dequantize this tensor to float32 (eager only). */
    std::vector<float> dequantized() const;

    /** Per-group scale vector + group size for quantized tensors. */
    const std::vector<float> &qScales() const { return qScales_; }
    int qGroup() const { return qGroup_; }
    const std::vector<uint8_t> &qBytes() const { return bytes_; }

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
    float get5(int i0, int i1, int i2, int i3, int i4) const;
    void  set5(int i0, int i1, int i2, int i3, int i4, float value);
    float get6(int i0, int i1, int i2, int i3, int i4, int i5) const;
    void  set6(int i0, int i1, int i2, int i3, int i4, int i5, float value);

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
    Tensor *gelu() const;
    Tensor *silu() const;
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
    Tensor *permute(const int *order, int rank) const;
    Tensor *reshape1(int d0) const;
    Tensor *reshape2(int d0, int d1) const;
    Tensor *reshape3(int d0, int d1, int d2) const;
    Tensor *reshape4(int d0, int d1, int d2, int d3) const;
    Tensor *reshape5(int d0, int d1, int d2, int d3, int d4) const;
    Tensor *reshape6(int d0, int d1, int d2, int d3, int d4, int d5) const;
    Tensor *flatten() const;

    float       *data();
    const float *data() const;

    void ensureEager(const char *op) const;

    static int product(const int *dims, int rank);

private:
    friend class TF;
    friend class Func;
    friend class CompiledFunction;
    friend class Graph;

    enum class Kind { Eager, Symbolic };

    void initDims(DType dtype, const int *dims, int rank);
    void checkSameShape(const Tensor *other, const char *op) const;
    int  offset2(int i0, int i1) const;
    int  offset3(int i0, int i1, int i2) const;
    int  offset4(int i0, int i1, int i2, int i3) const;
    int  offset5(int i0, int i1, int i2, int i3, int i4) const;
    int  offset6(int i0, int i1, int i2, int i3, int i4, int i5) const;

    Kind              kind_   = Kind::Eager;
    Graph            *graph_  = nullptr;
    int               nodeId_ = -1;
    int               rank_   = 0;
    int               dims_[kMaxRank] = {0, 0, 0, 0, 0, 0};
    int               size_   = 0;
    DType             dtype_  = DType::Float32;
    std::vector<float> data_;
    std::vector<uint8_t> bytes_;    // packed payload for quantized dtypes
    std::vector<float> qScales_;     // per-group scales (int8/int4)
    int               qGroup_ = 0;   // elements per scale group
    std::string       device_ = "cpu";
};

}  // namespace eve::tensor

#endif  // EVE_TENSOR_TENSOR_H
