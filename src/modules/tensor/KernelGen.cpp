#include "tensor/KernelGen.h"
#include "tensor/CpuKernels.h"
#include "tensor/Quant.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace eve::tensor {
namespace {

constexpr int kLocalSize = 256;

std::string header(int localX, int localY = 1) {
    std::ostringstream os;
    os << "#version 450\n";
    os << "layout(local_size_x = " << localX;
    if (localY > 1) os << ", local_size_y = " << localY;
    os << ") in;\n";
    return os.str();
}

std::string bufferDecl(int binding, const char *name) {
    std::ostringstream os;
    os << "layout(set = 0, binding = " << binding << ") buffer B" << binding << " { float "
       << name << "[]; };\n";
    return os.str();
}

std::string bufferDeclUint(int binding, const char *name) {
    std::ostringstream os;
    os << "layout(set = 0, binding = " << binding << ") buffer B" << binding << " { uint "
       << name << "[]; };\n";
    return os.str();
}

/**
 * GLSL dequantizer `float bval(uint elemIdx)` for a packed weight buffer
 * `bufName` (byte-addressed inside a uint SSBO) plus optional per-group
 * scales `bs`.
 */
std::string emitQuantizedBVal(DType dt, int group, const char *bufName = "b") {
    std::ostringstream os;
    const int g = group > 0 ? group : 1;
    switch (dt) {
        case DType::Int8:
            os << "float bval(uint idx) {\n"
               << "  uint word = " << bufName << "[idx >> 2u];\n"
               << "  uint byte = (word >> ((idx & 3u) * 8u)) & 0xFFu;\n"
               << "  int v = int(byte);\n"
               << "  if (v >= 128) v -= 256;\n"
               << "  return float(v) * bs[idx / " << g << "u];\n"
               << "}\n";
            break;
        case DType::Int4:
            os << "float bval(uint idx) {\n"
               << "  uint word = " << bufName << "[idx >> 3u];\n"
               << "  uint byte = (word >> (((idx >> 1u) & 3u) * 8u)) & 0xFFu;\n"
               << "  uint nib = (idx & 1u) == 0u ? (byte & 0xFu) : (byte >> 4u);\n"
               << "  int v = int(nib);\n"
               << "  if (v >= 8) v -= 16;\n"
               << "  return float(v) * bs[idx / " << g << "u];\n"
               << "}\n";
            break;
        case DType::Fp16:
            os << "float bval(uint idx) {\n"
               << "  uint word = " << bufName << "[idx >> 1u];\n"
               << "  uint hb = (word >> ((idx & 1u) * 16u)) & 0xFFFFu;\n"
               << "  return unpackHalf2x16(hb).x;\n"
               << "}\n";
            break;
        case DType::Fp8E4M3:
            os << "float bval(uint idx) {\n"
               << "  uint word = " << bufName << "[idx >> 2u];\n"
               << "  uint byte = (word >> ((idx & 3u) * 8u)) & 0xFFu;\n"
               << "  int s = (int(byte & 0x80u) != 0) ? -1 : 1;\n"
               << "  int e = int((byte >> 3u) & 0xFu);\n"
               << "  int m = int(byte & 0x7u);\n"
               << "  float v = (e == 0) ? exp2(-6.0) * float(m) / 8.0\n"
               << "                      : exp2(float(e - 7)) * (1.0 + float(m) / 8.0);\n"
               << "  return float(s) * v * bs[idx / " << g << "u];\n"
               << "}\n";
            break;
        case DType::Fp4E2M1:
            os << "float bval(uint idx) {\n"
               << "  uint word = " << bufName << "[idx >> 3u];\n"
               << "  uint byte = (word >> (((idx >> 1u) & 3u) * 8u)) & 0xFFu;\n"
               << "  uint nib = (idx & 1u) == 0u ? (byte & 0xFu) : (byte >> 4u);\n"
               << "  int s = (int(nib & 8u) != 0) ? -1 : 1;\n"
               << "  int e = int((nib >> 1u) & 3u);\n"
               << "  int m = int(nib & 1u);\n"
               << "  float v = (e == 0) ? 0.5 * float(m)\n"
               << "                      : exp2(float(e - 1)) * (1.0 + 0.5 * float(m));\n"
               << "  return float(s) * v * bs[idx / " << g << "u];\n"
               << "}\n";
            break;
        default: break;
    }
    return os.str();
}

std::string pushConstant() {
    return "layout(push_constant) uniform PC { float data[32]; } pc;\n";
}

int groupsFor(int count) { return (count + kLocalSize - 1) / kLocalSize; }

std::string scalarStr(float v) {
    if (v == int(v) && std::fabs(v) < 1e9f) return std::to_string(int(v)) + ".0";
    std::ostringstream os;
    os << v << "f";
    return os.str();
}

/**
 * Emit the elementwise chain expression. `rootVar` is the value entering the
 * first chain node (e.g. the matmul accumulator). Input fetches reference
 * buffers by `inputBind` + "[" + indexExpr + "]". Returns the final variable.
 */
struct ChainContext {
    const Graph &graph;
    const FusedGroup &group;
    const std::vector<std::string> &indexExprs;  // per group input
    std::string rootVar;
    std::ostringstream &os;
    int temp = 0;
    std::string biasIndexExpr;  // e.g. "jj" (matmul) / "f" (conv), empty = none

    std::string operand(int nodeId) {
        if (nodeId == group.biasNode && !biasIndexExpr.empty())
            return "bias[" + biasIndexExpr + "]";
        const auto it = std::find(group.inputs.begin(), group.inputs.end(), nodeId);
        if (it != group.inputs.end()) {
            const size_t idx = static_cast<size_t>(it - group.inputs.begin());
            return "a" + std::to_string(idx) + "[" + indexExprs[idx] + "]";
        }
        return vars[static_cast<size_t>(nodeId)];
    }

    std::vector<std::string> vars;

    std::string emit(int nodeId) {
        const GraphNode &nd = graph.node(nodeId);
        const std::string x = (nd.in0 == group.nodes.front() && rootVar != "") ? rootVar
                               : (nd.in0 >= 0 ? operand(nd.in0) : rootVar);
        std::string expr;
        switch (nd.type) {
            case OpType::Add: expr = "(" + x + " + " + operand(nd.in1) + ")"; break;
            case OpType::Sub: expr = "(" + x + " - " + operand(nd.in1) + ")"; break;
            case OpType::Multiply: expr = "(" + x + " * " + operand(nd.in1) + ")"; break;
            case OpType::Divide: expr = "(" + x + " / " + operand(nd.in1) + ")"; break;
            case OpType::AddScalar: expr = "(" + x + " + " + scalarStr(nd.s0) + ")"; break;
            case OpType::SubScalar: expr = "(" + x + " - " + scalarStr(nd.s0) + ")"; break;
            case OpType::MulScalar: expr = "(" + x + " * " + scalarStr(nd.s0) + ")"; break;
            case OpType::DivScalar: expr = "(" + x + " / " + scalarStr(nd.s0) + ")"; break;
            case OpType::PowScalar: expr = "pow(" + x + ", " + scalarStr(nd.s0) + ")"; break;
            case OpType::Neg: expr = "(-" + x + ")"; break;
            case OpType::Abs: expr = "abs(" + x + ")"; break;
            case OpType::Sqrt: expr = "sqrt(" + x + ")"; break;
            case OpType::Exp: expr = "exp(" + x + ")"; break;
            case OpType::Log: expr = "log(" + x + ")"; break;
            case OpType::Sin: expr = "sin(" + x + ")"; break;
            case OpType::Cos: expr = "cos(" + x + ")"; break;
            case OpType::Tanh: expr = "tanh(" + x + ")"; break;
            case OpType::Relu: expr = "max(" + x + ", 0.0)"; break;
            case OpType::Sigmoid: expr = "(1.0 / (1.0 + exp(-" + x + ")))"; break;
            case OpType::Gelu:
                expr = "(0.5 * " + x +
                       " * (1.0 + tanh(0.7978845608028654 * (" + x +
                       " + 0.044715 * " + x + " * " + x + " * " + x + "))))";
                break;
            case OpType::Silu: expr = "(" + x + " / (1.0 + exp(-" + x + ")))"; break;
            case OpType::Clamp: {
                float lo = nd.s0, hi = nd.s1;
                if (lo > hi) std::swap(lo, hi);
                expr = "clamp(" + x + ", " + scalarStr(lo) + ", " + scalarStr(hi) + ")";
                break;
            }
            case OpType::MaximumScalar: expr = "max(" + x + ", " + scalarStr(nd.s0) + ")"; break;
            case OpType::MinimumScalar: expr = "min(" + x + ", " + scalarStr(nd.s0) + ")"; break;
            case OpType::Where:
                expr = "(" + operand(nd.in0) + " > 0.5 ? " + operand(nd.in1) + " : " +
                       operand(nd.in2) + ")";
                break;
            default: return "";
        }
        const std::string var = "t" + std::to_string(temp++);
        os << "  float " << var << " = " << expr << ";\n";
        if (vars.size() <= static_cast<size_t>(nodeId)) vars.resize(static_cast<size_t>(nodeId) + 1);
        vars[static_cast<size_t>(nodeId)] = var;
        return var;
    }
};

/**
 * Per-input flat-index expression for an elementwise group. When an input has
 * the exact output shape, the identity index is used; otherwise a broadcast
 * index is decomposed from the output flat index with baked strides.
 */
void emitInputIndexExprs(std::ostringstream &os, const Graph &g, const FusedGroup &grp,
                         std::vector<std::string> &indexExprs) {
    const GraphNode &on = g.node(grp.outputNode);
    const int rank = on.rank;
    std::vector<int> S(static_cast<size_t>(rank), 1);
    if (rank > 0) {
        S[static_cast<size_t>(rank - 1)] = 1;
        for (int k = rank - 2; k >= 0; --k) S[static_cast<size_t>(k)] =
                                                S[static_cast<size_t>(k + 1)] * on.dims[k + 1];
    }
    indexExprs.resize(grp.inputs.size());
    for (size_t k = 0; k < grp.inputs.size(); ++k) {
        const GraphNode &inN = g.node(grp.inputs[k]);
        bool identical = inN.rank == rank;
        if (identical) {
            for (int d = 0; d < rank; ++d)
                if (inN.dims[d] != on.dims[d]) {
                    identical = false;
                    break;
                }
        }
        if (identical) {
            indexExprs[k] = "i_";
            continue;
        }
        const int pad = rank - inN.rank;
        std::vector<int> stride(static_cast<size_t>(rank), 0);
        for (int d = 0; d < rank; ++d) {
            const int dim = d < pad ? 1 : inN.dims[d - pad];
            if (dim == 1) continue;
            int s = 1;
            for (int t = d + 1; t < rank; ++t) {
                const int dt = t < pad ? 1 : inN.dims[t - pad];
                if (dt != 1) s *= dt;
            }
            stride[static_cast<size_t>(d)] = s;
        }
        const std::string var = "idx" + std::to_string(k);
        os << "  uint " << var << " = 0u;\n";
        for (int d = 0; d < rank; ++d) {
            if (stride[static_cast<size_t>(d)] == 0) continue;
            os << "  " << var << " += ((i_ / " << S[static_cast<size_t>(d)] << "u) % "
               << on.dims[d] << "u) * " << stride[static_cast<size_t>(d)] << "u;\n";
        }
        indexExprs[k] = var;
    }
}

bool genElementwise(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    if (grp.inputs.size() > size_t(kMaxKernelBindings - 1)) return false;
    const GraphNode &on = g.node(grp.outputNode);
    std::ostringstream os;
    os << header(kLocalSize);
    for (size_t k = 0; k < grp.inputs.size(); ++k)
        os << bufferDecl(int(k), ("a" + std::to_string(k)).c_str());
    os << bufferDecl(int(grp.inputs.size()), "o");
    os << pushConstant();
    os << "void main() {\n";
    os << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os << "  if (i_ >= " << on.size << "u) return;\n";
    std::vector<std::string> indexExprs;
    emitInputIndexExprs(os, g, grp, indexExprs);
    ChainContext ctx{g, grp, indexExprs, "", os, 0, ""};
    for (int u : grp.nodes) ctx.emit(u);
    os << "  o[i_] = " << ctx.vars[static_cast<size_t>(grp.outputNode)] << ";\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2 = os.str();
    out.groupsX2 = groupsFor(on.size);
    out.inputCount = int(grp.inputs.size());
    return true;
}

/** Emit matmul kernel; `tiled` selects the shared-memory 16x16 variant. */
bool genMatMul(const Graph &g, const FusedGroup &grp, bool tiled, KernelSpec &out) {
    const GraphNode &mm = g.node(grp.nodes.front());
    const GraphNode &A = g.node(mm.in0);
    const GraphNode &B = g.node(mm.in1);
    const bool batched = mm.rank == 3;
    const int batch = batched ? A.dims[0] : 1;
    const int m = A.dims[batched ? 1 : 0];
    const int k = A.dims[batched ? 2 : 1];
    const int n = B.dims[batched ? 2 : 1];
    const bool hasBias = grp.biasNode >= 0;
    const bool bQuant = q::isQuantDType(static_cast<DType>(B.dtype)) && !B.constBytes.empty();
    if (bQuant && tiled) return false;  // quantized weights use the naive variant only
    const int scalesBinding = hasBias ? 3 : 2;
    const int bindingOut = bQuant ? (hasBias ? 4 : 3) : (hasBias ? 3 : 2);

    std::ostringstream os;
    if (!tiled) {
        os << header(kLocalSize);
    } else {
        os << header(16, 16);
    }
    os << bufferDecl(0, "a");
    if (bQuant) os << bufferDeclUint(1, "b");
    else os << bufferDecl(1, "b");
    if (hasBias) os << bufferDecl(2, "bias");
    if (bQuant && B.dtype != static_cast<int>(DType::Fp16))
        os << bufferDecl(scalesBinding, "bs");
    os << bufferDecl(bindingOut, "o");
    os << pushConstant();
    if (bQuant) os << emitQuantizedBVal(static_cast<DType>(B.dtype), B.qGroup);

    std::vector<std::string> indexExprs(grp.inputs.size(), "i_");
    ChainContext ctx{g, grp, indexExprs, "r", os, 0, ""};
    ctx.biasIndexExpr = "jj";

    if (!tiled) {
        os << "void main() {\n";
        os << "  uint i_ = gl_GlobalInvocationID.x;\n";
        os << "  if (i_ >= " << batch * m * n << "u) return;\n";
        if (batched) {
            os << "  uint bb = i_ / " << m * n << "u;\n";
            os << "  uint rem = i_ % " << m * n << "u;\n";
            os << "  uint ii = rem / " << n << "u;\n";
            os << "  uint jj = rem % " << n << "u;\n";
        } else {
            os << "  uint ii = i_ / " << n << "u;\n";
            os << "  uint jj = i_ % " << n << "u;\n";
        }
        os << "  float r = 0.0;\n";
        os << "  for (uint t = 0u; t < " << k << "u; ++t) {\n";
        if (batched) {
            os << "    r += a[bb * " << m * k << "u + ii * " << k << "u + t] * "
               << (bQuant ? "bval(bb * " + std::to_string(k * n) + "u + t * " +
                                std::to_string(n) + "u + jj)"
                          : "b[bb * " + std::to_string(k * n) + "u + t * " +
                                std::to_string(n) + "u + jj]")
               << ";\n";
        } else {
            os << "    r += a[ii * " << k << "u + t] * "
               << (bQuant ? "bval(t * " + std::to_string(n) + "u + jj)"
                          : "b[t * " + std::to_string(n) + "u + jj]")
               << ";\n";
        }
        os << "  }\n";
        for (int u : grp.epilogue) ctx.emit(u);
        const std::string finalExpr =
            grp.epilogue.empty() ? std::string("r") : ctx.vars[static_cast<size_t>(grp.outputNode)];
        os << "  o[i_] = " << finalExpr << ";\n";
        os << "}\n";
        out.groupsX2 = groupsFor(batch * m * n);
    } else {
        ctx.biasIndexExpr = "col";
        const int gx = (n + 15) / 16;
        const int gy = (m + 15) / 16;
        os << "shared float As[16][17];\n";
        os << "shared float Bs[16][17];\n";
        os << "void main() {\n";
        os << "  uint tx = gl_LocalInvocationID.x;\n";
        os << "  uint ty = gl_LocalInvocationID.y;\n";
        os << "  uint row = gl_GlobalInvocationID.y;\n";
        os << "  uint col = gl_GlobalInvocationID.x;\n";
        os << "  float acc = 0.0;\n";
        os << "  for (uint tile = 0u; tile < " << (k + 15) / 16 << "u; ++tile) {\n";
        os << "    uint aCol = tile * 16u + tx;\n";
        os << "    uint bRow = tile * 16u + ty;\n";
        os << "    As[ty][tx] = (aCol < " << k << "u && row < " << m << "u) ? a[row * "
           << k << "u + aCol] : 0.0;\n";
        os << "    Bs[ty][tx] = (bRow < " << k << "u && col < " << n << "u) ? b[bRow * "
           << n << "u + col] : 0.0;\n";
        os << "    barrier();\n";
        os << "    for (uint t = 0u; t < 16u; ++t) acc += As[ty][t] * Bs[t][tx];\n";
        os << "    barrier();\n";
        os << "  }\n";
        os << "  float r = acc;\n";
        for (int u : grp.epilogue) ctx.emit(u);
        const std::string finalExpr =
            grp.epilogue.empty() ? std::string("r") : ctx.vars[static_cast<size_t>(grp.outputNode)];
        os << "  if (row < " << m << "u && col < " << n << "u) o[row * " << n << "u + col] = "
           << finalExpr << ";\n";
        os << "}\n";
        out.groupsX2 = gx;
        out.groupsY2 = gy;
    }
    out.pass1.clear();
    out.pass2 = os.str();
    out.inputCount = hasBias ? 3 : 2;
    out.qDtype = bQuant ? B.dtype : 0;
    out.qGroup = B.qGroup;
    out.scalesBinding = bQuant && B.dtype != static_cast<int>(DType::Fp16) ? scalesBinding : -1;
    out.outputBinding = bQuant ? bindingOut : -1;
    if (tiled && batched) return false;  // batched matmul uses the naive variant
    return true;
}

bool genConv(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &cn = g.node(grp.nodes.front());
    const GraphNode &X = g.node(cn.in0);
    const GraphNode &Wt = g.node(cn.in1);
    const bool is1d = cn.type == OpType::Conv1d;
    const int stride = cn.i0, pad = cn.i1;
    const bool hasBias = grp.biasNode >= 0;
    const int bindingOut = hasBias ? 3 : 2;

    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "x");
    os << bufferDecl(1, "w");
    if (hasBias) os << bufferDecl(2, "bias");
    os << bufferDecl(bindingOut, "o");
    os << pushConstant();
    std::vector<std::string> indexExprs(grp.inputs.size(), "i_");
    ChainContext ctx{g, grp, indexExprs, "r", os, 0, ""};
    ctx.biasIndexExpr = "f";

    os << "void main() {\n";
    os << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os << "  if (i_ >= " << g.node(grp.outputNode).size << "u) return;\n";
    if (is1d) {
        const int N = X.dims[0], C = X.dims[1], L = X.dims[2];
        const int F = Wt.dims[0], K = Wt.dims[2];
        os << "  uint n_ = i_ / (" << F << "u * " << cn.dims[2] << "u);\n";
        os << "  uint rem = i_ % (" << F << "u * " << cn.dims[2] << "u);\n";
        os << "  uint f = rem / " << cn.dims[2] << "u;\n";
        os << "  uint ol = rem % " << cn.dims[2] << "u;\n";
        os << "  float r = " << (hasBias ? "bias[f]" : "0.0") << ";\n";
        os << "  for (uint c = 0u; c < " << C << "u; ++c) {\n";
        os << "    for (uint kk = 0u; kk < " << K << "u; ++kk) {\n";
        os << "      int il = int(ol) * " << stride << " + int(kk) - " << pad << ";\n";
        os << "      if (il < 0 || il >= " << L << ") continue;\n";
        os << "      r += x[(n_ * " << C << "u + c) * " << L << "u + uint(il)] * w[(f * "
           << C << "u + c) * " << K << "u + kk];\n";
        os << "    }\n";
        os << "  }\n";
    } else {
        const int N = X.dims[0], C = X.dims[1], H = X.dims[2], W = X.dims[3];
        const int F = Wt.dims[0], KH = Wt.dims[2], KW = Wt.dims[3];
        os << "  uint n_ = i_ / (" << F << "u * " << cn.dims[2] << "u * " << cn.dims[3] << "u);\n";
        os << "  uint rem = i_ % (" << F << "u * " << cn.dims[2] << "u * " << cn.dims[3] << "u);\n";
        os << "  uint f = rem / (" << cn.dims[2] << "u * " << cn.dims[3] << "u);\n";
        os << "  uint rem2 = rem % (" << cn.dims[2] << "u * " << cn.dims[3] << "u);\n";
        os << "  uint oh = rem2 / " << cn.dims[3] << "u;\n";
        os << "  uint ow = rem2 % " << cn.dims[3] << "u;\n";
        os << "  float r = " << (hasBias ? "bias[f]" : "0.0") << ";\n";
        os << "  for (uint c = 0u; c < " << C << "u; ++c) {\n";
        os << "    for (uint kh = 0u; kh < " << KH << "u; ++kh) {\n";
        os << "      int ih = int(oh) * " << stride << " + int(kh) - " << pad << ";\n";
        os << "      if (ih < 0 || ih >= " << H << ") continue;\n";
        os << "      for (uint kw = 0u; kw < " << KW << "u; ++kw) {\n";
        os << "        int iw = int(ow) * " << stride << " + int(kw) - " << pad << ";\n";
        os << "        if (iw < 0 || iw >= " << W << ") continue;\n";
        os << "        r += x[((n_ * " << C << "u + c) * " << H << "u + uint(ih)) * " << W
           << "u + uint(iw)] * w[((f * " << C << "u + c) * " << KH << "u + kh) * " << KW
           << "u + kw];\n";
        os << "      }\n";
        os << "    }\n";
        os << "  }\n";
    }
    for (int u : grp.epilogue) ctx.emit(u);
    const std::string finalExpr =
        grp.epilogue.empty() ? std::string("r") : ctx.vars[static_cast<size_t>(grp.outputNode)];
    os << "  o[i_] = " << finalExpr << ";\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2 = os.str();
    out.groupsX2 = groupsFor(g.node(grp.outputNode).size);
    out.inputCount = hasBias ? 3 : 2;
    return true;
}

bool genPool(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &pn = g.node(grp.outputNode);
    const GraphNode &X = g.node(pn.in0);
    const int ksize = pn.i0, stride = pn.i1, pad = pn.i2;
    const int N = X.dims[0], C = X.dims[1], H = X.dims[2], W = X.dims[3];
    const int OH = pn.dims[2], OW = pn.dims[3];
    const bool maxPool = pn.type == OpType::MaxPool2d;
    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "in_");
    os << bufferDecl(1, "o");
    os << pushConstant();
    os << "void main() {\n";
    os << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os << "  if (i_ >= " << pn.size << "u) return;\n";
    os << "  uint n_ = i_ / (" << C << "u * " << OH << "u * " << OW << "u);\n";
    os << "  uint rem = i_ % (" << C << "u * " << OH << "u * " << OW << "u);\n";
    os << "  uint c = rem / (" << OH << "u * " << OW << "u);\n";
    os << "  uint rem2 = rem % (" << OH << "u * " << OW << "u);\n";
    os << "  uint oh = rem2 / " << OW << "u;\n";
    os << "  uint ow = rem2 % " << OW << "u;\n";
    os << "  float acc = " << (maxPool ? "-3.402823e38" : "0.0") << ";\n";
    os << "  int valid = 0;\n";
    os << "  for (int kh = 0; kh < " << ksize << "; ++kh) {\n";
    os << "    int ih = int(oh) * " << stride << " + kh - " << pad << ";\n";
    os << "    if (ih < 0 || ih >= " << H << ") continue;\n";
    os << "    for (int kw = 0; kw < " << ksize << "; ++kw) {\n";
    os << "      int iw = int(ow) * " << stride << " + kw - " << pad << ";\n";
    os << "      if (iw < 0 || iw >= " << W << ") continue;\n";
    os << "      float v = in_[((n_ * " << C << "u + c) * " << H << "u + uint(ih)) * " << W
       << "u + uint(iw)];\n";
    if (maxPool) {
        os << "      acc = max(acc, v);\n";
    } else {
        os << "      acc += v; ++valid;\n";
    }
    os << "    }\n";
    os << "  }\n";
    if (!maxPool) os << "  acc = valid > 0 ? acc / float(valid) : 0.0;\n";
    os << "  o[i_] = acc;\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2 = os.str();
    out.groupsX2 = groupsFor(pn.size);
    out.inputCount = 1;
    return true;
}

bool genSoftmax(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &sn = g.node(grp.outputNode);
    const GraphNode &X = g.node(sn.in0);
    const int axis = sn.i0;
    int outer = 1, reduce = 1, inner = 1;
    for (int k = 0; k < axis; ++k) outer *= X.dims[k];
    reduce = X.dims[axis];
    for (int k = axis + 1; k < X.rank; ++k) inner *= X.dims[k];
    const int rows = outer * inner;
    const bool logMode = grp.logMode;

    std::ostringstream os1, os2;
    os1 << header(kLocalSize);
    os1 << bufferDecl(0, "in_");
    os1 << bufferDecl(1, "mx");
    os1 << bufferDecl(2, "sm");
    os1 << pushConstant();
    os1 << "void main() {\n";
    os1 << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os1 << "  if (i_ >= " << rows << "u) return;\n";
    os1 << "  uint o_ = i_ / " << inner << "u;\n";
    os1 << "  uint ii = i_ % " << inner << "u;\n";
    os1 << "  float m = -3.402823e38;\n";
    os1 << "  for (uint j = 0u; j < " << reduce << "u; ++j) {\n";
    os1 << "    m = max(m, in_[(o_ * " << reduce << "u + j) * " << inner << "u + ii]);\n";
    os1 << "  }\n";
    os1 << "  float s = 0.0;\n";
    os1 << "  for (uint j = 0u; j < " << reduce << "u; ++j) {\n";
    os1 << "    s += exp(in_[(o_ * " << reduce << "u + j) * " << inner << "u + ii] - m);\n";
    os1 << "  }\n";
    os1 << "  mx[i_] = m;\n";
    os1 << "  sm[i_] = s;\n";
    os1 << "}\n";

    os2 << header(kLocalSize);
    os2 << bufferDecl(0, "in_");
    os2 << bufferDecl(1, "mx");
    os2 << bufferDecl(2, "sm");
    os2 << bufferDecl(3, "o");
    os2 << pushConstant();
    os2 << "void main() {\n";
    os2 << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os2 << "  if (i_ >= " << sn.size << "u) return;\n";
    os2 << "  uint o_ = (i_ / (" << reduce * inner << "u)) * " << inner << "u + (i_ % "
       << inner << "u);\n";
    os2 << "  float m = mx[o_];\n";
    os2 << "  float s = sm[o_];\n";
    os2 << "  float x = in_[i_];\n";
    if (logMode) {
        os2 << "  o[i_] = (x - m) - log(s);\n";
    } else {
        os2 << "  o[i_] = exp(x - m) / s;\n";
    }
    os2 << "}\n";
    out.pass1 = os1.str();
    out.pass2 = os2.str();
    out.groupsX1 = groupsFor(rows);
    out.groupsX2 = groupsFor(sn.size);
    out.inputCount = 1;
    out.inputsReadPass1 = 1;
    out.statsCount = 2;
    out.statsSize = rows;
    out.twoPass = true;
    return true;
}

bool genNorm(const Graph &g, const FusedGroup &grp, bool rms, KernelSpec &out) {
    const GraphNode &nn = g.node(grp.outputNode);
    const GraphNode &X = g.node(nn.in0);
    const int cols = X.dims[X.rank - 1];
    const int rows = X.size / cols;
    const float eps = nn.s0;
    const bool hasScale = grp.hasScale;
    const bool hasBias = !rms && grp.hasBias;
    const int inputCount = 1 + (hasScale ? 1 : 0) + (hasBias ? 1 : 0);
    const int statsCount = rms ? 1 : 2;
    const int outBinding = inputCount + statsCount;

    std::ostringstream os1, os2;
    os1 << header(kLocalSize);
    os1 << bufferDecl(0, "in_");
    os1 << bufferDecl(1, "st0");
    if (statsCount > 1) os1 << bufferDecl(2, "st1");
    os1 << pushConstant();
    os1 << "void main() {\n";
    os1 << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os1 << "  if (i_ >= " << rows << "u) return;\n";
    os1 << "  float s0 = 0.0;\n";
    if (statsCount > 1) os1 << "  float s1 = 0.0;\n";
    os1 << "  for (uint j = 0u; j < " << cols << "u; ++j) {\n";
    os1 << "    float v = in_[i_ * " << cols << "u + j];\n";
    if (rms) {
        os1 << "    s0 += v * v;\n";
    } else {
        os1 << "    s0 += v; s1 += v * v;\n";
    }
    os1 << "  }\n";
    os1 << "  st0[i_] = s0;\n";
    if (statsCount > 1) os1 << "  st1[i_] = s1;\n";
    os1 << "}\n";

    os2 << header(kLocalSize);
    for (int k = 0; k < inputCount; ++k)
        os2 << bufferDecl(k, ("a" + std::to_string(k)).c_str());
    os2 << bufferDecl(inputCount, "st0");
    if (statsCount > 1) os2 << bufferDecl(inputCount + 1, "st1");
    os2 << bufferDecl(outBinding, "o");
    os2 << pushConstant();
    os2 << "void main() {\n";
    os2 << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os2 << "  if (i_ >= " << nn.size << "u) return;\n";
    os2 << "  uint r = i_ / " << cols << "u;\n";
    os2 << "  uint c = i_ % " << cols << "u;\n";
    if (rms) {
        os2 << "  float inv = 1.0 / sqrt(st0[r] / " << cols << ".0 + " << scalarStr(eps)
            << ");\n";
        os2 << "  float y = a0[i_] * inv;\n";
        if (hasScale) os2 << "  y *= a1[c];\n";
    } else {
        os2 << "  float mean = st0[r] / " << cols << ".0;\n";
        os2 << "  float var = st1[r] / " << cols << ".0 - mean * mean;\n";
        os2 << "  var = max(var, 0.0);\n";
        os2 << "  float inv = 1.0 / sqrt(var + " << scalarStr(eps) << ");\n";
        os2 << "  float y = (a0[i_] - mean) * inv;\n";
        if (hasScale) os2 << "  y *= a1[c];\n";
        if (hasBias) os2 << "  y += a" << (hasScale ? 2 : 1) << "[c];\n";
    }
    os2 << "  o[i_] = y;\n";
    os2 << "}\n";
    out.pass1 = os1.str();
    out.pass2 = os2.str();
    out.groupsX1 = groupsFor(rows);
    out.groupsX2 = groupsFor(nn.size);
    out.inputCount = inputCount;
    out.inputsReadPass1 = 1;
    out.statsCount = statsCount;
    out.statsSize = rows;
    out.twoPass = true;
    return true;
}

bool genReduceOrArgmax(const Graph &g, const FusedGroup &grp, bool argmax, KernelSpec &out) {
    const GraphNode &rn = g.node(grp.outputNode);
    const GraphNode &X = g.node(rn.in0);
    const int axis = rn.i0;
    int outer = 1, reduce = 1, inner = 1;
    for (int k = 0; k < axis; ++k) outer *= X.dims[k];
    reduce = X.dims[axis];
    for (int k = axis + 1; k < X.rank; ++k) inner *= X.dims[k];
    const int outSize = outer * inner;
    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "in_");
    os << bufferDecl(1, "o");
    os << pushConstant();
    os << "void main() {\n";
    os << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os << "  if (i_ >= " << outSize << "u) return;\n";
    os << "  uint o_ = i_ / " << inner << "u;\n";
    os << "  uint ii = i_ % " << inner << "u;\n";
    if (argmax) {
        os << "  float best = -3.402823e38;\n";
        os << "  float bestJ = 0.0;\n";
        os << "  for (uint j = 0u; j < " << reduce << "u; ++j) {\n";
        os << "    float v = in_[(o_ * " << reduce << "u + j) * " << inner << "u + ii];\n";
        os << "    if (v > best) { best = v; bestJ = float(j); }\n";
        os << "  }\n";
        os << "  o[i_] = bestJ;\n";
    } else {
        switch (grp.op) {
            case OpType::ReduceSum:
            case OpType::ReduceMean:
                os << "  float acc = 0.0;\n";
                os << "  for (uint j = 0u; j < " << reduce << "u; ++j) acc += in_[(o_ * "
                   << reduce << "u + j) * " << inner << "u + ii];\n";
                if (grp.op == OpType::ReduceMean)
                    os << "  acc /= " << scalarStr(float(reduce)) << ";\n";
                break;
            case OpType::ReduceMin:
                os << "  float acc = 3.402823e38;\n";
                os << "  for (uint j = 0u; j < " << reduce << "u; ++j) acc = min(acc, in_[(o_ * "
                   << reduce << "u + j) * " << inner << "u + ii]);\n";
                break;
            case OpType::ReduceMax:
                os << "  float acc = -3.402823e38;\n";
                os << "  for (uint j = 0u; j < " << reduce << "u; ++j) acc = max(acc, in_[(o_ * "
                   << reduce << "u + j) * " << inner << "u + ii]);\n";
                break;
            default: return false;
        }
        os << "  o[i_] = acc;\n";
    }
    os << "}\n";
    out.pass1.clear();
    out.pass2 = os.str();
    out.groupsX2 = groupsFor(outSize);
    out.inputCount = 1;
    return true;
}

bool genEmbedding(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &en = g.node(grp.outputNode);
    const GraphNode &T = g.node(en.in0);
    const GraphNode &I = g.node(en.in1);
    const bool tQuant = q::isQuantDType(static_cast<DType>(T.dtype)) && !T.constBytes.empty();
    const bool tInt = T.dtype != static_cast<int>(DType::Fp16);
    const int vocab = T.dims[0], dim = T.dims[1];
    std::ostringstream os;
    os << header(kLocalSize);
    if (tQuant) os << bufferDeclUint(0, "table");
    else os << bufferDecl(0, "table");
    os << bufferDecl(1, "idx");
    if (tQuant && tInt) os << bufferDecl(2, "bs");
    os << bufferDecl(tQuant ? 3 : 2, "o");
    os << pushConstant();
    if (tQuant) os << emitQuantizedBVal(static_cast<DType>(T.dtype), T.qGroup, "table");
    os << "void main() {\n";
    os << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os << "  if (i_ >= " << en.size << "u) return;\n";
    os << "  uint r = i_ / " << dim << "u;\n";
    os << "  uint d = i_ % " << dim << "u;\n";
    os << "  int ii = int(idx[r]);\n";
    os << "  ii = clamp(ii, 0, " << (vocab - 1) << ");\n";
    os << "  o[i_] = " << (tQuant ? "bval(uint(ii) * " + std::to_string(dim) + "u + d)"
                                  : "table[uint(ii) * " + std::to_string(dim) + "u + d]")
       << ";\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2 = os.str();
    out.groupsX2 = groupsFor(en.size);
    out.inputCount = 2;
    out.qDtype = tQuant ? T.dtype : 0;
    out.qGroup = T.qGroup;
    out.scalesBinding = tQuant && tInt ? 2 : -1;
    out.outputBinding = tQuant ? 3 : -1;
    return true;
}

bool genConcat(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &cn = g.node(grp.outputNode);
    const int axis = cn.i0;
    const int n = int(grp.inputs.size());
    if (n < 2 || n > 4) return false;
    int starts[4] = {};
    int axisTotal = 0;
    for (int k = 0; k < n; ++k) {
        starts[k] = axisTotal;
        axisTotal += g.node(grp.inputs[static_cast<size_t>(k)]).dims[axis];
    }
    int inner = 1;
    for (int k = axis + 1; k < cn.rank; ++k) inner *= cn.dims[k];
    std::ostringstream os;
    os << header(kLocalSize);
    for (int k = 0; k < n; ++k) os << bufferDecl(k, ("a" + std::to_string(k)).c_str());
    os << bufferDecl(n, "o");
    os << pushConstant();
    os << "void main() {\n";
    os << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os << "  if (i_ >= " << cn.size << "u) return;\n";
    os << "  uint ax = (i_ / " << inner << "u) % " << axisTotal << "u;\n";
    os << "  uint op = i_ / (" << axisTotal << "u * " << inner << "u);\n";
    os << "  uint ip = i_ % " << inner << "u;\n";
    os << "  float v = 0.0;\n";
    for (int k = 0; k < n; ++k) {
        const int sz = g.node(grp.inputs[static_cast<size_t>(k)]).dims[axis];
        const char *cond = k == 0 ? "if" : "else if";
        os << "  " << cond << " (ax >= " << starts[k] << "u && ax < " << starts[k] + sz
           << "u) {\n";
        os << "    v = a" << k << "[op * " << sz << "u * " << inner << "u + (ax - " << starts[k]
           << "u) * " << inner << "u + ip];\n";
        os << "  }\n";
    }
    os << "  o[i_] = v;\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2 = os.str();
    out.groupsX2 = groupsFor(cn.size);
    out.inputCount = n;
    return true;
}

bool genSlice(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &sn = g.node(grp.outputNode);
    const GraphNode &X = g.node(sn.in0);
    const int axis = sn.i0, begin = sn.i1, end = sn.i2;
    const int axisSize = end - begin;
    int inner = 1;
    for (int k = axis + 1; k < sn.rank; ++k) inner *= sn.dims[k];
    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "in_");
    os << bufferDecl(1, "o");
    os << pushConstant();
    os << "void main() {\n";
    os << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os << "  if (i_ >= " << sn.size << "u) return;\n";
    os << "  uint ax = (i_ / " << inner << "u) % " << axisSize << "u;\n";
    os << "  uint op = i_ / (" << axisSize << "u * " << inner << "u);\n";
    os << "  uint ip = i_ % " << inner << "u;\n";
    os << "  o[i_] = in_[op * " << X.dims[axis] << "u * " << inner << "u + (ax + " << begin
       << "u) * " << inner << "u + ip];\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2 = os.str();
    out.groupsX2 = groupsFor(sn.size);
    out.inputCount = 1;
    return true;
}

bool genPermute(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &pn = g.node(grp.outputNode);
    const GraphNode &X = g.node(pn.in0);
    const int rank = pn.rank;
    int S[Tensor::kMaxRank] = {};
    S[rank - 1] = 1;
    for (int k = rank - 2; k >= 0; --k) S[k] = S[k + 1] * pn.dims[k + 1];
    int inStride[Tensor::kMaxRank] = {};
    inStride[rank - 1] = 1;
    for (int k = rank - 2; k >= 0; --k) inStride[k] = inStride[k + 1] * X.dims[k + 1];
    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "in_");
    os << bufferDecl(1, "o");
    os << pushConstant();
    os << "void main() {\n";
    os << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os << "  if (i_ >= " << pn.size << "u) return;\n";
    os << "  uint idx = 0u;\n";
    for (int k = 0; k < rank; ++k) {
        const int inAxis = pn.perm[k];
        os << "  idx += ((i_ / " << S[k] << "u) % " << pn.dims[k] << "u) * "
           << inStride[inAxis] << "u;\n";
    }
    os << "  o[i_] = in_[idx];\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2 = os.str();
    out.groupsX2 = groupsFor(pn.size);
    out.inputCount = 1;
    return true;
}

bool genResize2d(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &rn = g.node(grp.outputNode);
    const GraphNode &X = g.node(rn.in0);
    const int H = X.dims[2], W = X.dims[3];
    const int OH = rn.dims[2], OW = rn.dims[3];
    const bool nearest = rn.i0 == 0;
    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "in_");
    os << bufferDecl(1, "o");
    os << pushConstant();
    os << "void main() {\n";
    os << "  uint i_ = gl_GlobalInvocationID.x;\n";
    os << "  if (i_ >= " << rn.size << "u) return;\n";
    os << "  uint ow = i_ % " << OW << "u;\n";
    os << "  uint rem = i_ / " << OW << "u;\n";
    os << "  uint oh = rem % " << OH << "u;\n";
    os << "  uint rem2 = rem / " << OH << "u;\n";
    os << "  uint c = rem2 % " << X.dims[1] << "u;\n";
    os << "  uint n_ = rem2 / " << X.dims[1] << "u;\n";
    os << "  uint base = (n_ * " << X.dims[1] << "u + c) * " << H << "u * " << W << "u;\n";
    if (nearest) {
        os << "  uint ih = uint(float(oh) * " << scalarStr(float(H) / OH) << ") ;\n";
        os << "  uint iw = uint(float(ow) * " << scalarStr(float(W) / OW) << ") ;\n";
        os << "  ih = min(ih, " << H - 1 << "u); iw = min(iw, " << W - 1 << "u);\n";
        os << "  o[i_] = in_[base + ih * " << W << "u + iw];\n";
    } else {
        os << "  float fx = float(ow) * " << scalarStr(float(W) / OW) << " - 0.5;\n";
        os << "  float fy = float(oh) * " << scalarStr(float(H) / OH) << " - 0.5;\n";
        os << "  fx = clamp(fx, 0.0, " << scalarStr(float(W - 1)) << ");\n";
        os << "  fy = clamp(fy, 0.0, " << scalarStr(float(H - 1)) << ");\n";
        os << "  uint x0 = uint(floor(fx)); uint y0 = uint(floor(fy));\n";
        os << "  uint x1 = min(x0 + 1u, " << W - 1 << "u); uint y1 = min(y0 + 1u, " << H - 1
           << "u);\n";
        os << "  float w00 = in_[base + y0 * " << W << "u + x0];\n";
        os << "  float w10 = in_[base + y0 * " << W << "u + x1];\n";
        os << "  float w01 = in_[base + y1 * " << W << "u + x0];\n";
        os << "  float w11 = in_[base + y1 * " << W << "u + x1];\n";
        os << "  float top = w00 + (w10 - w00) * (fx - float(x0));\n";
        os << "  float bot = w01 + (w11 - w01) * (fx - float(x0));\n";
        os << "  o[i_] = top + (bot - top) * (fy - float(y0));\n";
    }
    os << "}\n";
    out.pass1.clear();
    out.pass2 = os.str();
    out.groupsX2 = groupsFor(rn.size);
    out.inputCount = 1;
    return true;
}

bool genSdpa(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &qn = g.node(grp.outputNode);
    const GraphNode &Q = g.node(qn.in0);
    const GraphNode &K = g.node(qn.in1);
    const int B = Q.dims[0], H = Q.dims[1], T = Q.dims[2], D = Q.dims[3];
    const int S = K.dims[2];
    if (S > 2048 || D > 512) return false;  // shared-memory limits -> CPU fallback
    const float scale = qn.s0;
    const bool masked = grp.masked;
    const int bindingMask = masked ? 3 : -1;
    const int bindingOut = masked ? 4 : 3;
    std::ostringstream os;
    os << header(128);
    os << bufferDecl(0, "q");
    os << bufferDecl(1, "k");
    os << bufferDecl(2, "v");
    if (masked) os << bufferDecl(3, "mask");
    os << bufferDecl(bindingOut, "o");
    os << "shared float scores[" << S << "];\n";
    os << "shared float maxv;\n";
    os << "shared float sumv;\n";
    os << pushConstant();
    os << "void main() {\n";
    os << "  uint tid = gl_LocalInvocationID.x;\n";
    os << "  uint bh = gl_WorkGroupID.x;\n";
    os << "  uint t = gl_WorkGroupID.y;\n";
    os << "  uint b = bh / " << H << "u;\n";
    os << "  uint h = bh % " << H << "u;\n";
    os << "  uint qbase = (b * " << H << "u + h) * " << T << "u * " << D << "u + t * " << D
       << "u;\n";
    os << "  uint kbase = (b * " << H << "u + h) * " << S << "u * " << D << "u;\n";
    os << "  uint vbase = kbase;\n";
    os << "  for (uint s = tid; s < " << S << "u; s += 128u) {\n";
    os << "    float acc = 0.0;\n";
    os << "    for (uint d = 0u; d < " << D << "u; ++d) acc += q[qbase + d] * k[kbase + s * "
       << D << "u + d];\n";
    os << "    acc *= " << scalarStr(scale) << ";\n";
    if (masked) {
        os << "    acc += mask[(b * " << H << "u + h) * " << T << "u * " << S << "u + t * " << S
           << "u + s];\n";
    }
    os << "    scores[s] = acc;\n";
    os << "  }\n";
    os << "  barrier();\n";
    os << "  if (tid == 0u) {\n";
    os << "    float m = -3.402823e38;\n";
    os << "    for (uint s = 0u; s < " << S << "u; ++s) m = max(m, scores[s]);\n";
    os << "    float sm = 0.0;\n";
    os << "    for (uint s = 0u; s < " << S << "u; ++s) sm += exp(scores[s] - m);\n";
    os << "    maxv = m; sumv = sm;\n";
    os << "  }\n";
    os << "  barrier();\n";
    os << "  for (uint d = tid; d < " << D << "u; d += 128u) {\n";
    os << "    float acc = 0.0;\n";
    os << "    for (uint s = 0u; s < " << S << "u; ++s) acc += exp(scores[s] - maxv) * v[vbase + s * "
       << D << "u + d];\n";
    os << "    o[qbase + d] = acc / sumv;\n";
    os << "  }\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2 = os.str();
    out.groupsX2 = B * H;
    out.groupsY2 = T;
    out.inputCount = masked ? 4 : 3;
    return true;
}

}  // namespace

bool generateKernel(const Graph &graph, const FusedGroup &group, KernelSpec &out) {
    out = KernelSpec{};
    switch (group.kind) {
        case GroupKind::Elementwise: return genElementwise(graph, group, out);
        case GroupKind::MatMul:
            // naive variant first; the runtime autotunes between naive and tiled
            return genMatMul(graph, group, false, out);
        case GroupKind::Conv1d:
        case GroupKind::Conv2d: return genConv(graph, group, out);
        case GroupKind::MaxPool2d:
        case GroupKind::AvgPool2d: return genPool(graph, group, out);
        case GroupKind::Softmax: return genSoftmax(graph, group, out);
        case GroupKind::LayerNorm: return genNorm(graph, group, false, out);
        case GroupKind::RMSNorm: return genNorm(graph, group, true, out);
        case GroupKind::Reduce:
        case GroupKind::ArgMax:
            return genReduceOrArgmax(graph, group, group.kind == GroupKind::ArgMax, out);
        case GroupKind::Embedding: return genEmbedding(graph, group, out);
        case GroupKind::Concat: return genConcat(graph, group, out);
        case GroupKind::Slice: return genSlice(graph, group, out);
        case GroupKind::Permute: return genPermute(graph, group, out);
        case GroupKind::Resize2d: return genResize2d(graph, group, out);
        case GroupKind::Sdpa: return genSdpa(graph, group, out);
        case GroupKind::Alias: return true;  // no kernel; pure buffer alias
    }
    return false;
}

bool generateMatMulVariant(const Graph &graph, const FusedGroup &group, bool tiled,
                           KernelSpec &out) {
    out = KernelSpec{};
    if (group.kind != GroupKind::MatMul) return false;
    return genMatMul(graph, group, tiled, out);
}

}  // namespace eve::tensor
