
#include "common/Export.h"
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
 * @brief Tensor element types.
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
/** @brief True when quant d type. */
bool isQuantDType(DType dt);
}

/** @brief Dtype name. */
const char *dtypeName(DType dtype);
/** @brief Parse d type. */
bool parseDType(const std::string &name, DType &out);

/**
 * @brief float32 / int32 tensor (rank 1–6), row-major.
 * Eager: owns a buffer. Symbolic: node in a Func graph (no buffer until run).
 */
class EVENGINE_API_DOMAINS Tensor {
public:
    static constexpr int kMaxRank = 6;

    /** @brief Tensor. */
    Tensor() = default;
    /** @brief 从 dims[0..rank) 创建 eager 张量。 */
    explicit Tensor(const int *dims, int rank);
    /** @brief Tensor. */
    explicit Tensor(DType dtype, const int *dims, int rank);
    /** @brief 按秩创建全零 eager 张量。 */
    Tensor(int d0);
    /** @brief Tensor. */
    Tensor(int d0, int d1);
    /** @brief Tensor. */
    Tensor(int d0, int d1, int d2);
    /** @brief Tensor. */
    Tensor(int d0, int d1, int d2, int d3);
    /** @brief Tensor. */
    Tensor(int d0, int d1, int d2, int d3, int d4);
    /** @brief Tensor. */
    Tensor(int d0, int d1, int d2, int d3, int d4, int d5);

    /** @brief Symbolic handle into a graph node. */
    static Tensor *makeSymbolic(Graph *graph, int nodeId, const int *dims, int rank);

    /** @brief True when symbolic. */
    bool        isSymbolic() const { return kind_ == Kind::Symbolic; }
    /** @brief True when eager. */
    bool        isEager() const { return kind_ == Kind::Eager; }
    /** @brief Graph. */
    Graph      *graph() const { return graph_; }
    /** @brief Node id. */
    int         nodeId() const { return nodeId_; }

    /** @brief Returns the rank. */
    int         getRank() const { return rank_; }
    /** @brief Byte length of the owned buffer. */
    int         getSize() const { return size_; }
    /** @brief Returns the dim. */
    int         getDim(int axis) const;
    /** @brief Returns the dim 0. */
    int         getDim0() const { return dims_[0]; }
    /** @brief Returns the dim 1. */
    int         getDim1() const { return dims_[1]; }
    /** @brief Returns the dim 2. */
    int         getDim2() const { return dims_[2]; }
    /** @brief Returns the dim 3. */
    int         getDim3() const { return dims_[3]; }
    /** @brief Returns the dim 4. */
    int         getDim4() const { return dims_[4]; }
    /** @brief Returns the dim 5. */
    int         getDim5() const { return dims_[5]; }
    /** @brief Returns the device. */
    std::string getDevice() const { return device_; }
    /** @brief Returns the dtype. */
    std::string getDtype() const { return dtypeName(dtype_); }
    /** @brief Dtype. */
    DType       dtype() const { return dtype_; }
    /** @brief Sets the dtype. */
    void        setDtype(DType dtype) { dtype_ = dtype; }

    /** True for packed weight-quantization dtypes (bytes_, qScales_). */
    /** @brief True when quantized. */
    bool isQuantized() const { return q::isQuantDType(dtype_); }

    /** Dequantize this tensor to float32 (eager only). */
    /** @brief Dequantized. */
    std::vector<float> dequantized() const;

    /** Per-group scale vector + group size for quantized tensors. */
    /** @brief Q scales. */
    const std::vector<float> &qScales() const { return qScales_; }
    /** @brief Q group. */
    int qGroup() const { return qGroup_; }
    /** @brief Q bytes. */
    const std::vector<uint8_t> &qBytes() const { return bytes_; }

    /** @brief Returns the get. */
    float get(int flatIndex) const;
    /** @brief Sets the set. */
    void  set(int flatIndex, float value);
    /** @brief Returns the 1. */
    float get1(int i0) const;
    /** @brief Sets the 1. */
    void  set1(int i0, float value);
    /** @brief Returns the 2. */
    float get2(int i0, int i1) const;
    /** @brief Sets the 2. */
    void  set2(int i0, int i1, float value);
    /** @brief Returns the 3. */
    float get3(int i0, int i1, int i2) const;
    /** @brief Sets the 3. */
    void  set3(int i0, int i1, int i2, float value);
    /** @brief Returns the 4. */
    float get4(int i0, int i1, int i2, int i3) const;
    /** @brief Sets the 4. */
    void  set4(int i0, int i1, int i2, int i3, float value);
    /** @brief Returns the 5. */
    float get5(int i0, int i1, int i2, int i3, int i4) const;
    /** @brief Sets the 5. */
    void  set5(int i0, int i1, int i2, int i3, int i4, float value);
    /** @brief Returns the 6. */
    float get6(int i0, int i1, int i2, int i3, int i4, int i5) const;
    /** @brief Sets the 6. */
    void  set6(int i0, int i1, int i2, int i3, int i4, int i5, float value);

    /** @brief Fill. */
    void fill(float value);
    /** @brief Copies from. */
    void copyFrom(const Tensor *other);
    /** @brief Deep copy. @ownership Caller deletes. */
    Tensor *clone() const;

    /** @brief Eager 逐元素运算（符号张量会抛异常）。 */
    Tensor *add(const Tensor *other) const;
    /** @brief Sub. */
    Tensor *sub(const Tensor *other) const;
    /** @brief Multiply. */
    Tensor *multiply(const Tensor *other) const;
    /** @brief Div. */
    Tensor *div(const Tensor *other) const;
    /** @brief Adds scalar. */
    Tensor *addScalar(float s) const;
    /** @brief Sub scalar. */
    Tensor *subScalar(float s) const;
    /** @brief Mul scalar. */
    Tensor *mulScalar(float s) const;
    /** @brief Div scalar. */
    Tensor *divScalar(float s) const;
    /** @brief Neg. */
    Tensor *neg() const;
    /** @brief Abs. */
    Tensor *abs() const;
    /** @brief Sqrt. */
    Tensor *sqrt() const;
    /** @brief Exp. */
    Tensor *exp() const;
    /** @brief Log. */
    Tensor *log() const;
    /** @brief Sin. */
    Tensor *sin() const;
    /** @brief Cos. */
    Tensor *cos() const;
    /** @brief Tanh. */
    Tensor *tanh() const;
    /** @brief Relu. */
    Tensor *relu() const;
    /** @brief Sigmoid. */
    Tensor *sigmoid() const;
    /** @brief Gelu. */
    Tensor *gelu() const;
    /** @brief Silu. */
    Tensor *silu() const;
    /** @brief Pow scalar. */
    Tensor *powScalar(float exp) const;
    /** @brief Clamp. */
    Tensor *clamp(float lo, float hi) const;
    /** @brief Maximum scalar. */
    Tensor *maximumScalar(float s) const;
    /** @brief Minimum scalar. */
    Tensor *minimumScalar(float s) const;

    /** @brief Eager 原地运算。 */
    void addInPlace(const Tensor *other);
    /** @brief Multiply in place. */
    void multiplyInPlace(const Tensor *other);
    /** @brief Adds scalar in place. */
    void addScalarInPlace(float s);
    /** @brief Mul scalar in place. */
    void mulScalarInPlace(float s);
    /** @brief Relu in place. */
    void reluInPlace();

    /** @brief 归约：求和 / 均值 / 最小 / 最大。 */
    float reduceSum() const;
    /** @brief Reduce mean. */
    float reduceMean() const;
    /** @brief Reduce min. */
    float reduceMin() const;
    /** @brief Reduce max. */
    float reduceMax() const;
    /** @brief Dot. */
    float dot(const Tensor *other) const;

    /** @brief 矩阵乘法 / 转置 / 变形。 */
    Tensor *matmul(const Tensor *other) const;
    /** @brief Transpose. */
    Tensor *transpose() const;
    /** @brief Permute. */
    Tensor *permute(const int *order, int rank) const;
    /** @brief Reshape 1. */
    Tensor *reshape1(int d0) const;
    /** @brief Reshape 2. */
    Tensor *reshape2(int d0, int d1) const;
    /** @brief Reshape 3. */
    Tensor *reshape3(int d0, int d1, int d2) const;
    /** @brief Reshape 4. */
    Tensor *reshape4(int d0, int d1, int d2, int d3) const;
    /** @brief Reshape 5. */
    Tensor *reshape5(int d0, int d1, int d2, int d3, int d4) const;
    /** @brief Reshape 6. */
    Tensor *reshape6(int d0, int d1, int d2, int d3, int d4, int d5) const;
    /** @brief Flatten. */
    Tensor *flatten() const;

    /** @brief 原始数据指针（eager）。 */
    float       *data();
    /** @brief Data. */
    const float *data() const;

    /** @brief Ensure eager. */
    void ensureEager(const char *op) const;

    /** @brief Product. */
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
