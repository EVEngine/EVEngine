#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>
#include "common/Exception.h"
#include "tensor/CpuKernels.h"
#include "tensor/KernelGenInternal.h"
#include "tensor/KernelGenWgslInternal.h"
#include "tensor/Quant.h"

// KernelGenResize.cpp
namespace eve::tensor::glsl_detail {
void genResize2d(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode   &rn = g.node(grp.outputNode);
    const GraphNode   &X  = g.node(rn.in0);
    const int          H = X.dims[2], W = X.dims[3];
    const int          OH = rn.dims[2], OW = rn.dims[3];
    const bool         nearest = rn.i0 == 0;
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
        os << "  float fx = (float(ow) + 0.5) * " << scalarStr(float(W) / OW) << " - 0.5;\n";
        os << "  float fy = (float(oh) + 0.5) * " << scalarStr(float(H) / OH) << " - 0.5;\n";
        os << "  fx = clamp(fx, 0.0, " << scalarStr(float(W - 1)) << ");\n";
        os << "  fy = clamp(fy, 0.0, " << scalarStr(float(H - 1)) << ");\n";
        os << "  uint x0 = uint(floor(fx)); uint y0 = uint(floor(fy));\n";
        os << "  uint x1 = min(x0 + 1u, " << W - 1 << "u); uint y1 = min(y0 + 1u, " << H - 1 << "u);\n";
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
    out.pass2      = os.str();
    out.groupsX2   = groupsFor(rn.size);
    out.inputCount = 1;
    return;
}


}  // namespace eve::tensor::glsl_detail

// KernelGenWgsl.cpp
namespace eve::tensor {
namespace wgsl_detail {


std::string header(int localX, int localY) {
    return "const workgroupX = " + std::to_string(localX) + "u;\nconst workgroupY = " + std::to_string(localY) + "u;\n";
}
std::string bufferDecl(int binding, const char *name) {
    return "@group(0) @binding(" + std::to_string(binding) + ") var<storage, read_write> " + name + ": array<f32>;\n";
}
std::string bufferDeclUint(int binding, const char *name) {
    return "@group(0) @binding(" + std::to_string(binding) + ") var<storage, read_write> " + name + ": array<u32>;\n";
}

/**
 * WGSL dequantizer `float bval(uint elemIdx)` for a packed weight buffer
 * `bufName` (byte-addressed inside a uint SSBO) plus optional per-group
 * scales `bs`.
 */
std::string emitQuantizedBVal(DType dt, int group, const char *bufName = "b") {
    std::ostringstream os;
    const int          g = group > 0 ? group : 1;
    switch (dt) {
        case DType::Int8:
            os << "fn bval(idx: u32) -> f32 {\n"
               << "  var word: u32 = " << bufName << "[idx >> 2u];\n"
               << "  var byte: u32 = (word >> ((idx & 3u) * 8u)) & 0xFFu;\n"
               << "  var v: i32 = i32(byte);\n"
               << "  if (v >= 128) { v -= 256; }\n"
               << "  return f32(v) * bs[idx / " << g << "u];\n"
               << "}\n";
            break;
        case DType::Int4:
            os << "fn bval(idx: u32) -> f32 {\n"
               << "  var word: u32 = " << bufName << "[idx >> 3u];\n"
               << "  var byte: u32 = (word >> (((idx >> 1u) & 3u) * 8u)) & 0xFFu;\n"
               << "  var nib: u32 = select(byte >> 4u, byte & 0xFu, (idx & 1u) == 0u);\n"
               << "  var v: i32 = i32(nib);\n"
               << "  if (v >= 8) { v -= 16; }\n"
               << "  return f32(v) * bs[idx / " << g << "u];\n"
               << "}\n";
            break;
        case DType::Fp16:
            os << "fn bval(idx: u32) -> f32 {\n"
               << "  var word: u32 = " << bufName << "[idx >> 1u];\n"
               << "  var hb: u32 = (word >> ((idx & 1u) * 16u)) & 0xFFFFu;\n"
               << "  return unpack2x16float(hb).x;\n"
               << "}\n";
            break;
        case DType::Fp8E4M3:
            os << "fn bval(idx: u32) -> f32 {\n"
               << "  var word: u32 = " << bufName << "[idx >> 2u];\n"
               << "  var byte: u32 = (word >> ((idx & 3u) * 8u)) & 0xFFu;\n"
               << "  var s: i32 = select(1, -1, (byte & 0x80u) != 0u);\n"
               << "  var e: i32 = i32((byte >> 3u) & 0xFu);\n"
               << "  var m: i32 = i32(byte & 0x7u);\n"
               << "  var v: f32 = select(exp2(f32(e - 7)) * (1.0 + f32(m) / 8.0),\n"
               << "                      exp2(-6.0) * f32(m) / 8.0, e == 0);\n"
               << "  return f32(s) * v * bs[idx / " << g << "u];\n"
               << "}\n";
            break;
        case DType::Fp4E2M1:
            os << "fn bval(idx: u32) -> f32 {\n"
               << "  var word: u32 = " << bufName << "[idx >> 3u];\n"
               << "  var byte: u32 = (word >> (((idx >> 1u) & 3u) * 8u)) & 0xFFu;\n"
               << "  var nib: u32 = select(byte >> 4u, byte & 0xFu, (idx & 1u) == 0u);\n"
               << "  var s: i32 = select(1, -1, (nib & 8u) != 0u);\n"
               << "  var e: i32 = i32((nib >> 1u) & 3u);\n"
               << "  var m: i32 = i32(nib & 1u);\n"
               << "  var v: f32 = select(exp2(f32(e - 1)) * (1.0 + 0.5 * f32(m)),\n"
               << "                      0.5 * f32(m), e == 0);\n"
               << "  return f32(s) * v * bs[idx / " << g << "u];\n"
               << "}\n";
            break;
        default: break;
    }
    return os.str();
}

std::string pushConstant() { return ""; }

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
    const Graph                    &graph;
    const FusedGroup               &group;
    const std::vector<std::string> &indexExprs;  // per group input
    std::string                     rootVar;
    std::ostringstream             &os;
    int                             temp = 0;
    std::string                     biasIndexExpr;  // e.g. "jj" (matmul) / "f" (conv), empty = none

    std::string operand(int nodeId) {
        if (nodeId == group.biasNode && !biasIndexExpr.empty()) return "bias[" + biasIndexExpr + "]";
        const auto it = std::find(group.inputs.begin(), group.inputs.end(), nodeId);
        if (it != group.inputs.end()) {
            const size_t idx = static_cast<size_t>(it - group.inputs.begin());
            return "a" + std::to_string(idx) + "[" + indexExprs[idx] + "]";
        }
        return vars[static_cast<size_t>(nodeId)];
    }

    std::vector<std::string> vars;

    std::string emit(int nodeId) {
        const GraphNode  &nd = graph.node(nodeId);
        const std::string x =
            (nd.in0 == group.nodes.front() && rootVar != "") ? rootVar : (nd.in0 >= 0 ? operand(nd.in0) : rootVar);
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
                expr = "(0.5 * " + x + " * (1.0 + tanh(0.7978845608028654 * (" + x + " + 0.044715 * " + x + " * " + x +
                       " * " + x + "))))";
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
                expr = "select(" + operand(nd.in2) + ", " + operand(nd.in1) + ", " + operand(nd.in0) + " > 0.5)";
                break;
            default: return "";
        }
        const std::string var = "t" + std::to_string(temp++);
        os << "  var " << var << ": f32 = " << expr << ";\n";
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
    const GraphNode &on   = g.node(grp.outputNode);
    const int        rank = on.rank;
    std::vector<int> S(static_cast<size_t>(rank), 1);
    if (rank > 0) {
        S[static_cast<size_t>(rank - 1)] = 1;
        for (int k = rank - 2; k >= 0; --k) S[static_cast<size_t>(k)] = S[static_cast<size_t>(k + 1)] * on.dims[k + 1];
    }
    indexExprs.resize(grp.inputs.size());
    for (size_t k = 0; k < grp.inputs.size(); ++k) {
        const GraphNode &inN       = g.node(grp.inputs[k]);
        bool             identical = inN.rank == rank;
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
        const int        pad = rank - inN.rank;
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
        os << "  var " << var << ": u32 = 0u;\n";
        for (int d = 0; d < rank; ++d) {
            if (stride[static_cast<size_t>(d)] == 0) continue;
            os << "  " << var << " += ((i_ / " << S[static_cast<size_t>(d)] << "u) % " << on.dims[d] << "u) * "
               << stride[static_cast<size_t>(d)] << "u;\n";
        }
        indexExprs[k] = var;
    }
}

void genElementwise(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    if (grp.inputs.size() > size_t(kMaxKernelBindings - 1))
        throw eve::Exception("Tensor WGSL: unsupported kernel variant or binding count");
    const GraphNode   &on = g.node(grp.outputNode);
    std::ostringstream os;
    os << header(kLocalSize);
    for (size_t k = 0; k < grp.inputs.size(); ++k) os << bufferDecl(int(k), ("a" + std::to_string(k)).c_str());
    os << bufferDecl(int(grp.inputs.size()), "o");
    os << pushConstant();
    os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
          "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) {\n";
    os << "  var i_: u32 = globalId.x;\n";
    os << "  if (i_ >= " << on.size << "u) { return; }\n";
    std::vector<std::string> indexExprs;
    emitInputIndexExprs(os, g, grp, indexExprs);
    ChainContext ctx{g, grp, indexExprs, "", os, 0, ""};
    for (int u : grp.nodes) ctx.emit(u);
    os << "  o[i_] = " << ctx.vars[static_cast<size_t>(grp.outputNode)] << ";\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2      = os.str();
    out.groupsX2   = groupsFor(on.size);
    out.inputCount = int(grp.inputs.size());
    return;
}

/** Emit matmul kernel; `tiled` selects the shared-memory 16x16 variant. */
void genMatMul(const Graph &g, const FusedGroup &grp, bool tiled, KernelSpec &out) {
    const GraphNode &mm      = g.node(grp.nodes.front());
    const GraphNode &A       = g.node(mm.in0);
    const GraphNode &B       = g.node(mm.in1);
    const bool       batched = mm.rank == 3;
    const int        batch   = batched ? A.dims[0] : 1;
    const int        m       = A.dims[batched ? 1 : 0];
    const int        k       = A.dims[batched ? 2 : 1];
    const int        n       = B.dims[batched ? 2 : 1];
    const bool       hasBias = grp.biasNode >= 0;
    const bool       bQuant  = q::isQuantDType(static_cast<DType>(B.dtype)) && !B.constBytes.empty();
    if (bQuant && tiled)
        throw eve::Exception("Tensor WGSL: unsupported kernel variant or binding count");  // quantized weights use the
                                                                                           // naive variant only
    const int scalesBinding = hasBias ? 3 : 2;
    const int bindingOut    = bQuant ? (hasBias ? 4 : 3) : (hasBias ? 3 : 2);

    std::ostringstream os;
    if (!tiled) {
        os << header(kLocalSize);
    } else {
        os << header(16, 16);
    }
    os << bufferDecl(0, "a");
    if (bQuant)
        os << bufferDeclUint(1, "b");
    else
        os << bufferDecl(1, "b");
    if (hasBias) os << bufferDecl(2, "bias");
    if (bQuant && B.dtype != static_cast<int>(DType::Fp16)) os << bufferDecl(scalesBinding, "bs");
    os << bufferDecl(bindingOut, "o");
    os << pushConstant();
    if (bQuant) os << emitQuantizedBVal(static_cast<DType>(B.dtype), B.qGroup);

    std::vector<std::string> indexExprs(grp.inputs.size(), "i_");
    ChainContext             ctx{g, grp, indexExprs, "r", os, 0, ""};
    ctx.biasIndexExpr = "jj";

    if (!tiled) {
        os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
              "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) "
              "{\n";
        os << "  var i_: u32 = globalId.x;\n";
        os << "  if (i_ >= " << batch * m * n << "u) { return; }\n";
        if (batched) {
            os << "  var bb: u32 = i_ / " << m * n << "u;\n";
            os << "  var rem: u32 = i_ % " << m * n << "u;\n";
            os << "  var ii: u32 = rem / " << n << "u;\n";
            os << "  var jj: u32 = rem % " << n << "u;\n";
        } else {
            os << "  var ii: u32 = i_ / " << n << "u;\n";
            os << "  var jj: u32 = i_ % " << n << "u;\n";
        }
        os << "  var r: f32 = 0.0;\n";
        os << "  for (var t: u32 = 0u; t < " << k << "u; t++) {\n";
        if (batched) {
            os << "    r += a[bb * " << m * k << "u + ii * " << k << "u + t] * "
               << (bQuant ? "bval(bb * " + std::to_string(k * n) + "u + t * " + std::to_string(n) + "u + jj)"
                          : "b[bb * " + std::to_string(k * n) + "u + t * " + std::to_string(n) + "u + jj]")
               << ";\n";
        } else {
            os << "    r += a[ii * " << k << "u + t] * "
               << (bQuant ? "bval(t * " + std::to_string(n) + "u + jj)" : "b[t * " + std::to_string(n) + "u + jj]")
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
        const int gx      = (n + 15) / 16;
        const int gy      = (m + 15) / 16;
        os << "var<workgroup> As: array<array<f32, 17>, 16>;\n";
        os << "var<workgroup> Bs: array<array<f32, 17>, 16>;\n";
        os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
              "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) "
              "{\n";
        os << "  var tx: u32 = localId.x;\n";
        os << "  var ty: u32 = localId.y;\n";
        os << "  var row: u32 = globalId.y;\n";
        os << "  var col: u32 = globalId.x;\n";
        os << "  var acc: f32 = 0.0;\n";
        os << "  for (var tile: u32 = 0u; tile < " << (k + 15) / 16 << "u; tile++) {\n";
        os << "    var aCol: u32 = tile * 16u + tx;\n";
        os << "    var bRow: u32 = tile * 16u + ty;\n";
        os << "    As[ty][tx] = 0.0; Bs[ty][tx] = 0.0;\n";
        os << "    if (aCol < " << k << "u && row < " << m << "u) { As[ty][tx] = a[row * " << k << "u + aCol]; }\n";
        os << "    if (bRow < " << k << "u && col < " << n << "u) { Bs[ty][tx] = b[bRow * " << n << "u + col]; }\n";
        os << "    workgroupBarrier();\n";
        os << "    for (var t: u32 = 0u; t < 16u; t++) { acc += As[ty][t] * Bs[t][tx]; }\n";
        os << "    workgroupBarrier();\n";
        os << "  }\n";
        os << "  if (row >= " << m << "u || col >= " << n << "u) { return; }\n";
        os << "  var i_: u32 = row * " << n << "u + col;\n";
        os << "  var r: f32 = acc;\n";
        for (int u : grp.epilogue) ctx.emit(u);
        const std::string finalExpr =
            grp.epilogue.empty() ? std::string("r") : ctx.vars[static_cast<size_t>(grp.outputNode)];
        os << "  o[i_] = " << finalExpr << ";\n}\n";
        out.groupsX2 = gx;
        out.groupsY2 = gy;
    }
    out.pass1.clear();
    out.pass2         = os.str();
    out.inputCount    = hasBias ? 3 : 2;
    out.qDtype        = bQuant ? B.dtype : 0;
    out.qGroup        = B.qGroup;
    out.scalesBinding = bQuant && B.dtype != static_cast<int>(DType::Fp16) ? scalesBinding : -1;
    out.outputBinding = bQuant ? bindingOut : -1;
    if (tiled && batched)
        throw eve::Exception(
            "Tensor WGSL: unsupported kernel variant or binding count");  // batched matmul uses the naive variant
    return;
}

void genConv(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &cn     = g.node(grp.nodes.front());
    const GraphNode &X      = g.node(cn.in0);
    const GraphNode &Wt     = g.node(cn.in1);
    const bool       is1d   = cn.type == OpType::Conv1d;
    const int        stride = cn.i0, pad = cn.i1;
    const bool       hasBias     = grp.biasNode >= 0;
    const bool       hasConvBias = cn.in2 >= 0;
    const int        bindingOut  = 2 + int(hasConvBias) + int(hasBias);

    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "x");
    os << bufferDecl(1, "w");
    if (hasConvBias) os << bufferDecl(2, "convBias");
    if (hasBias) os << bufferDecl(hasConvBias ? 3 : 2, "bias");
    os << bufferDecl(bindingOut, "o");
    os << pushConstant();
    std::vector<std::string> indexExprs(grp.inputs.size(), "i_");
    ChainContext             ctx{g, grp, indexExprs, "r", os, 0, ""};
    ctx.biasIndexExpr = is1d ? "ol" : "ow";

    os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
          "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) {\n";
    os << "  var i_: u32 = globalId.x;\n";
    os << "  if (i_ >= " << g.node(grp.outputNode).size << "u) { return; }\n";
    if (is1d) {
        const int C = X.dims[1], L = X.dims[2];
        const int F = Wt.dims[0], K = Wt.dims[2];
        os << "  var n_: u32 = i_ / (" << F << "u * " << cn.dims[2] << "u);\n";
        os << "  var rem: u32 = i_ % (" << F << "u * " << cn.dims[2] << "u);\n";
        os << "  var f: u32 = rem / " << cn.dims[2] << "u;\n";
        os << "  var ol: u32 = rem % " << cn.dims[2] << "u;\n";
        os << "  var r: f32 = " << (hasConvBias ? "convBias[f]" : "0.0") << ";\n";
        os << "  for (var c: u32 = 0u; c < " << C << "u; c++) {\n";
        os << "    for (var kk: u32 = 0u; kk < " << K << "u; kk++) {\n";
        os << "      var il: i32 = i32(ol) * " << stride << " + i32(kk) - " << pad << ";\n";
        os << "      if (il < 0 || il >= " << L << ") { continue; }\n";
        os << "      r += x[(n_ * " << C << "u + c) * " << L << "u + u32(il)] * w[(f * " << C << "u + c) * " << K
           << "u + kk];\n";
        os << "    }\n";
        os << "  }\n";
    } else {
        const int C = X.dims[1], H = X.dims[2], W = X.dims[3];
        const int F = Wt.dims[0], KH = Wt.dims[2], KW = Wt.dims[3];
        os << "  var n_: u32 = i_ / (" << F << "u * " << cn.dims[2] << "u * " << cn.dims[3] << "u);\n";
        os << "  var rem: u32 = i_ % (" << F << "u * " << cn.dims[2] << "u * " << cn.dims[3] << "u);\n";
        os << "  var f: u32 = rem / (" << cn.dims[2] << "u * " << cn.dims[3] << "u);\n";
        os << "  var rem2: u32 = rem % (" << cn.dims[2] << "u * " << cn.dims[3] << "u);\n";
        os << "  var oh: u32 = rem2 / " << cn.dims[3] << "u;\n";
        os << "  var ow: u32 = rem2 % " << cn.dims[3] << "u;\n";
        os << "  var r: f32 = " << (hasConvBias ? "convBias[f]" : "0.0") << ";\n";
        os << "  for (var c: u32 = 0u; c < " << C << "u; c++) {\n";
        os << "    for (var kh: u32 = 0u; kh < " << KH << "u; kh++) {\n";
        os << "      var ih: i32 = i32(oh) * " << stride << " + i32(kh) - " << pad << ";\n";
        os << "      if (ih < 0 || ih >= " << H << ") { continue; }\n";
        os << "      for (var kw: u32 = 0u; kw < " << KW << "u; kw++) {\n";
        os << "        var iw: i32 = i32(ow) * " << stride << " + i32(kw) - " << pad << ";\n";
        os << "        if (iw < 0 || iw >= " << W << ") { continue; }\n";
        os << "        r += x[((n_ * " << C << "u + c) * " << H << "u + u32(ih)) * " << W << "u + u32(iw)] * w[((f * "
           << C << "u + c) * " << KH << "u + kh) * " << KW << "u + kw];\n";
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
    out.pass2      = os.str();
    out.groupsX2   = groupsFor(g.node(grp.outputNode).size);
    out.inputCount = 2 + int(hasConvBias) + int(hasBias);
    return;
}

void genPool(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode   &pn    = g.node(grp.outputNode);
    const GraphNode   &X     = g.node(pn.in0);
    const int          ksize = pn.i0, stride = pn.i1, pad = pn.i2;
    const int          C = X.dims[1], H = X.dims[2], W = X.dims[3];
    const int          OH = pn.dims[2], OW = pn.dims[3];
    const bool         maxPool = pn.type == OpType::MaxPool2d;
    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "in_");
    os << bufferDecl(1, "o");
    os << pushConstant();
    os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
          "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) {\n";
    os << "  var i_: u32 = globalId.x;\n";
    os << "  if (i_ >= " << pn.size << "u) { return; }\n";
    os << "  var n_: u32 = i_ / (" << C << "u * " << OH << "u * " << OW << "u);\n";
    os << "  var rem: u32 = i_ % (" << C << "u * " << OH << "u * " << OW << "u);\n";
    os << "  var c: u32 = rem / (" << OH << "u * " << OW << "u);\n";
    os << "  var rem2: u32 = rem % (" << OH << "u * " << OW << "u);\n";
    os << "  var oh: u32 = rem2 / " << OW << "u;\n";
    os << "  var ow: u32 = rem2 % " << OW << "u;\n";
    os << "  var acc: f32 = " << (maxPool ? "-3.402823e38" : "0.0") << ";\n";
    os << "  var valid: i32 = 0;\n";
    os << "  for (var kh: i32 = 0; kh < " << ksize << "; kh++) {\n";
    os << "    var ih: i32 = i32(oh) * " << stride << " + kh - " << pad << ";\n";
    os << "    if (ih < 0 || ih >= " << H << ") { continue; }\n";
    os << "    for (var kw: i32 = 0; kw < " << ksize << "; kw++) {\n";
    os << "      var iw: i32 = i32(ow) * " << stride << " + kw - " << pad << ";\n";
    os << "      if (iw < 0 || iw >= " << W << ") { continue; }\n";
    os << "      var v: f32 = in_[((n_ * " << C << "u + c) * " << H << "u + u32(ih)) * " << W << "u + u32(iw)];\n";
    if (maxPool) {
        os << "      acc = max(acc, v);\n";
    } else {
        os << "      acc += v; valid++;\n";
    }
    os << "    }\n";
    os << "  }\n";
    if (!maxPool) os << "  if (valid > 0) { acc /= f32(valid); }\n";
    os << "  o[i_] = acc;\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2      = os.str();
    out.groupsX2   = groupsFor(pn.size);
    out.inputCount = 1;
    return;
}

}  // namespace wgsl_detail

void generateKernelWgslImpl(const Graph &graph, const FusedGroup &group, KernelSpec &out) {
    using namespace wgsl_detail;
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
        case GroupKind::ArgMax: return genReduceOrArgmax(graph, group, group.kind == GroupKind::ArgMax, out);
        case GroupKind::Embedding: return genEmbedding(graph, group, out);
        case GroupKind::Concat: return genConcat(graph, group, out);
        case GroupKind::Slice: return genSlice(graph, group, out);
        case GroupKind::Permute: return genPermute(graph, group, out);
        case GroupKind::Resize2d: return genResize2d(graph, group, out);
        case GroupKind::Sdpa: return genSdpa(graph, group, out);
        case GroupKind::Alias: return;  // no kernel; pure buffer alias
    }
    throw eve::Exception("Tensor WGSL: unsupported kernel variant or binding count");
}

Result<KernelSpec> generateWgslKernel(const Graph &graph, const FusedGroup &group, KernelVariant variant) {
    try {
        KernelSpec out;
        if (variant == KernelVariant::TiledMatMul) {
            if (group.kind != GroupKind::MatMul)
                return Result<KernelSpec>::failure(Diagnostic::error(
                    DiagnosticCode::Unsupported, "Tiled WGSL requires a matrix product", "tensor.kernel"));
            wgsl_detail::genMatMul(graph, group, true, out);
        } else {
            generateKernelWgslImpl(graph, group, out);
        }
        wgsl_detail::specializeInputBindings(graph, group, out);
        return Result<KernelSpec>::success(std::move(out));
    } catch (const std::exception &error) {
        return Result<KernelSpec>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, error.what(), "tensor.kernel"));
    }
}

}  // namespace eve::tensor

// KernelGenWgslBindings.cpp
namespace eve::tensor::wgsl_detail {
namespace {
int storageNode(const Graph &graph, int id) {
    const auto &node = graph.node(id);
    switch (node.type) {
        case OpType::Reshape:
        case OpType::Flatten:
        case OpType::Cast: return storageNode(graph, node.in0);
        default: return id;
    }
}

// Specialize only our generated declarations and identifier tokens, never arbitrary user shaders.
void specializeSource(std::string &source, const std::vector<int> &representatives, int inputCount) {
    std::vector<std::string>           names(static_cast<size_t>(inputCount));
    std::map<std::string, std::string> aliases;
    for (int i = 0; i < inputCount; ++i) {
        const std::string prefix = "@group(0) @binding(" + std::to_string(i) + ") var<storage, read_write> ";
        const auto        begin  = source.find(prefix);
        if (begin == std::string::npos) throw eve::Exception("Tensor WGSL: missing input declaration");
        const auto nameBegin          = begin + prefix.size();
        const auto nameEnd            = source.find(':', nameBegin);
        names[static_cast<size_t>(i)] = source.substr(nameBegin, nameEnd - nameBegin);
        if (representatives[static_cast<size_t>(i)] == i) continue;
        aliases.emplace(names[static_cast<size_t>(i)],
                        names[static_cast<size_t>(representatives[static_cast<size_t>(i)])]);
        source.erase(begin, source.find('\n', nameEnd) + 1 - begin);
    }
    if (aliases.empty()) return;
    std::string renamed;
    for (size_t i = 0; i < source.size();) {
        const unsigned char ch = static_cast<unsigned char>(source[i]);
        if (std::isalpha(ch) || ch == '_') {
            const auto begin = i++;
            while (i < source.size() && (std::isalnum(static_cast<unsigned char>(source[i])) || source[i] == '_')) ++i;
            const std::string word  = source.substr(begin, i - begin);
            const auto        found = aliases.find(word);
            renamed += found == aliases.end() ? word : found->second;
        } else {
            renamed += source[i++];
        }
    }
    source = std::move(renamed);
}
}  // namespace

void specializeInputBindings(const Graph &graph, const FusedGroup &group, KernelSpec &spec) {
    std::map<int, int> first;
    for (int i = 0; i < spec.inputCount; ++i) {
        const auto [it, inserted] = first.emplace(storageNode(graph, group.inputs[static_cast<size_t>(i)]), i);
        (void)inserted;
        spec.inputRepresentatives.push_back(it->second);
    }
    if (spec.twoPass) specializeSource(spec.pass1, spec.inputRepresentatives, spec.inputsReadPass1);
    if (!spec.pass2.empty()) specializeSource(spec.pass2, spec.inputRepresentatives, spec.inputCount);
}
}  // namespace eve::tensor::wgsl_detail

// KernelGenWgslIndex.cpp
namespace eve::tensor::wgsl_detail {
void genEmbedding(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode   &en     = g.node(grp.outputNode);
    const GraphNode   &T      = g.node(en.in0);
    const bool         tQuant = q::isQuantDType(static_cast<DType>(T.dtype)) && !T.constBytes.empty();
    const bool         tInt   = T.dtype != static_cast<int>(DType::Fp16);
    const int          vocab = T.dims[0], dim = T.dims[1];
    std::ostringstream os;
    os << header(kLocalSize);
    if (tQuant)
        os << bufferDeclUint(0, "table");
    else
        os << bufferDecl(0, "table");
    os << bufferDecl(1, "idx");
    if (tQuant && tInt) os << bufferDecl(2, "bs");
    os << bufferDecl(tQuant ? 3 : 2, "o");
    os << pushConstant();
    if (tQuant) os << emitQuantizedBVal(static_cast<DType>(T.dtype), T.qGroup, "table");
    os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
          "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) {\n";
    os << "  var i_: u32 = globalId.x;\n";
    os << "  if (i_ >= " << en.size << "u) { return; }\n";
    os << "  var r: u32 = i_ / " << dim << "u;\n";
    os << "  var d: u32 = i_ % " << dim << "u;\n";
    os << "  var ii: i32 = i32(idx[r]);\n";
    os << "  ii = clamp(ii, 0, " << (vocab - 1) << ");\n";
    os << "  o[i_] = "
       << (tQuant ? "bval(u32(ii) * " + std::to_string(dim) + "u + d)"
                  : "table[u32(ii) * " + std::to_string(dim) + "u + d]")
       << ";\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2         = os.str();
    out.groupsX2      = groupsFor(en.size);
    out.inputCount    = 2;
    out.qDtype        = tQuant ? T.dtype : 0;
    out.qGroup        = T.qGroup;
    out.scalesBinding = tQuant && tInt ? 2 : -1;
    out.outputBinding = tQuant ? 3 : -1;
    return;
}

void genConcat(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &cn   = g.node(grp.outputNode);
    const int        axis = cn.i0;
    const int        n    = int(grp.inputs.size());
    if (n < 2 || n > 4) throw eve::Exception("Tensor WGSL: unsupported kernel variant or binding count");
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
    os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
          "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) {\n";
    os << "  var i_: u32 = globalId.x;\n";
    os << "  if (i_ >= " << cn.size << "u) { return; }\n";
    os << "  var ax: u32 = (i_ / " << inner << "u) % " << axisTotal << "u;\n";
    os << "  var op: u32 = i_ / (" << axisTotal << "u * " << inner << "u);\n";
    os << "  var ip: u32 = i_ % " << inner << "u;\n";
    os << "  var v: f32 = 0.0;\n";
    for (int k = 0; k < n; ++k) {
        const int   sz   = g.node(grp.inputs[static_cast<size_t>(k)]).dims[axis];
        const char *cond = k == 0 ? "if" : "else if";
        os << "  " << cond << " (ax >= " << starts[k] << "u && ax < " << starts[k] + sz << "u) {\n";
        os << "    v = a" << k << "[op * " << sz << "u * " << inner << "u + (ax - " << starts[k] << "u) * " << inner
           << "u + ip];\n";
        os << "  }\n";
    }
    os << "  o[i_] = v;\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2      = os.str();
    out.groupsX2   = groupsFor(cn.size);
    out.inputCount = n;
    return;
}

void genSlice(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &sn   = g.node(grp.outputNode);
    const GraphNode &X    = g.node(sn.in0);
    const int        axis = sn.i0, begin = sn.i1, end = sn.i2;
    const int        axisSize = end - begin;
    int              inner    = 1;
    for (int k = axis + 1; k < sn.rank; ++k) inner *= sn.dims[k];
    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "in_");
    os << bufferDecl(1, "o");
    os << pushConstant();
    os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
          "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) {\n";
    os << "  var i_: u32 = globalId.x;\n";
    os << "  if (i_ >= " << sn.size << "u) { return; }\n";
    os << "  var ax: u32 = (i_ / " << inner << "u) % " << axisSize << "u;\n";
    os << "  var op: u32 = i_ / (" << axisSize << "u * " << inner << "u);\n";
    os << "  var ip: u32 = i_ % " << inner << "u;\n";
    os << "  o[i_] = in_[op * " << X.dims[axis] << "u * " << inner << "u + (ax + " << begin << "u) * " << inner
       << "u + ip];\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2      = os.str();
    out.groupsX2   = groupsFor(sn.size);
    out.inputCount = 1;
    return;
}

void genPermute(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &pn                  = g.node(grp.outputNode);
    const GraphNode &X                   = g.node(pn.in0);
    const int        rank                = pn.rank;
    int              S[Tensor::kMaxRank] = {};
    S[rank - 1]                          = 1;
    for (int k = rank - 2; k >= 0; --k) S[k] = S[k + 1] * pn.dims[k + 1];
    int inStride[Tensor::kMaxRank] = {};
    inStride[rank - 1]             = 1;
    for (int k = rank - 2; k >= 0; --k) inStride[k] = inStride[k + 1] * X.dims[k + 1];
    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "in_");
    os << bufferDecl(1, "o");
    os << pushConstant();
    os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
          "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) {\n";
    os << "  var i_: u32 = globalId.x;\n";
    os << "  if (i_ >= " << pn.size << "u) { return; }\n";
    os << "  var idx: u32 = 0u;\n";
    for (int k = 0; k < rank; ++k) {
        const int inAxis = pn.perm[k];
        os << "  idx += ((i_ / " << S[k] << "u) % " << pn.dims[k] << "u) * " << inStride[inAxis] << "u;\n";
    }
    os << "  o[i_] = in_[idx];\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2      = os.str();
    out.groupsX2   = groupsFor(pn.size);
    out.inputCount = 1;
    return;
}

void genResize2d(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode   &rn = g.node(grp.outputNode);
    const GraphNode   &X  = g.node(rn.in0);
    const int          H = X.dims[2], W = X.dims[3];
    const int          OH = rn.dims[2], OW = rn.dims[3];
    const bool         nearest = rn.i0 == 0;
    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "in_");
    os << bufferDecl(1, "o");
    os << pushConstant();
    os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
          "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) {\n";
    os << "  var i_: u32 = globalId.x;\n";
    os << "  if (i_ >= " << rn.size << "u) { return; }\n";
    os << "  var ow: u32 = i_ % " << OW << "u;\n";
    os << "  var rem: u32 = i_ / " << OW << "u;\n";
    os << "  var oh: u32 = rem % " << OH << "u;\n";
    os << "  var rem2: u32 = rem / " << OH << "u;\n";
    os << "  var c: u32 = rem2 % " << X.dims[1] << "u;\n";
    os << "  var n_: u32 = rem2 / " << X.dims[1] << "u;\n";
    os << "  var base: u32 = (n_ * " << X.dims[1] << "u + c) * " << H << "u * " << W << "u;\n";
    if (nearest) {
        os << "  var ih: u32 = u32(f32(oh) * " << scalarStr(float(H) / OH) << ") ;\n";
        os << "  var iw: u32 = u32(f32(ow) * " << scalarStr(float(W) / OW) << ") ;\n";
        os << "  ih = min(ih, " << H - 1 << "u); iw = min(iw, " << W - 1 << "u);\n";
        os << "  o[i_] = in_[base + ih * " << W << "u + iw];\n";
    } else {
        os << "  var fx: f32 = (f32(ow) + 0.5) * " << scalarStr(float(W) / OW) << " - 0.5;\n";
        os << "  var fy: f32 = (f32(oh) + 0.5) * " << scalarStr(float(H) / OH) << " - 0.5;\n";
        os << "  fx = clamp(fx, 0.0, " << scalarStr(float(W - 1)) << ");\n";
        os << "  fy = clamp(fy, 0.0, " << scalarStr(float(H - 1)) << ");\n";
        os << "  var x0: u32 = u32(floor(fx)); var y0: u32 = u32(floor(fy));\n";
        os << "  var x1: u32 = min(x0 + 1u, " << W - 1 << "u); var y1: u32 = min(y0 + 1u, " << H - 1 << "u);\n";
        os << "  var w00: f32 = in_[base + y0 * " << W << "u + x0];\n";
        os << "  var w10: f32 = in_[base + y0 * " << W << "u + x1];\n";
        os << "  var w01: f32 = in_[base + y1 * " << W << "u + x0];\n";
        os << "  var w11: f32 = in_[base + y1 * " << W << "u + x1];\n";
        os << "  var top: f32 = w00 + (w10 - w00) * (fx - f32(x0));\n";
        os << "  var bot: f32 = w01 + (w11 - w01) * (fx - f32(x0));\n";
        os << "  o[i_] = top + (bot - top) * (fy - f32(y0));\n";
    }
    os << "}\n";
    out.pass1.clear();
    out.pass2      = os.str();
    out.groupsX2   = groupsFor(rn.size);
    out.inputCount = 1;
    return;
}

void genSdpa(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &qn = g.node(grp.outputNode);
    const GraphNode &Q  = g.node(qn.in0);
    const GraphNode &K  = g.node(qn.in1);
    const int        B = Q.dims[0], H = Q.dims[1], T = Q.dims[2], D = Q.dims[3];
    const int        S = K.dims[2];
    if (S > 2048 || D > 512)
        throw eve::Exception(
            "Tensor WGSL: unsupported kernel variant or binding count");  // shared-memory limits -> CPU fallback
    const float        scale      = qn.s0;
    const bool         masked     = grp.masked;
    const int          bindingOut = masked ? 4 : 3;
    std::ostringstream os;
    os << header(128);
    os << bufferDecl(0, "q");
    os << bufferDecl(1, "k");
    os << bufferDecl(2, "v");
    if (masked) os << bufferDecl(3, "mask");
    os << bufferDecl(bindingOut, "o");
    os << "var<workgroup> scores: array<f32, " << S << ">;\n";
    os << "var<workgroup> maxv: f32;\n";
    os << "var<workgroup> sumv: f32;\n";
    os << pushConstant();
    os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
          "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) {\n";
    os << "  var tid: u32 = localId.x;\n";
    os << "  var bh: u32 = groupId.x;\n";
    os << "  var t: u32 = groupId.y;\n";
    os << "  var b: u32 = bh / " << H << "u;\n";
    os << "  var h: u32 = bh % " << H << "u;\n";
    os << "  var qbase: u32 = (b * " << H << "u + h) * " << T << "u * " << D << "u + t * " << D << "u;\n";
    os << "  var kbase: u32 = (b * " << H << "u + h) * " << S << "u * " << D << "u;\n";
    os << "  var vbase: u32 = kbase;\n";
    os << "  for (var s: u32 = tid; s < " << S << "u; s += 128u) {\n";
    os << "    var acc: f32 = 0.0;\n";
    os << "    for (var d: u32 = 0u; d < " << D << "u; d++) { acc += q[qbase + d] * k[kbase + s * " << D
       << "u + d]; }\n";
    os << "    acc *= " << scalarStr(scale) << ";\n";
    if (masked) {
        os << "    acc += mask[(b * " << H << "u + h) * " << T << "u * " << S << "u + t * " << S << "u + s];\n";
    }
    os << "    scores[s] = acc;\n";
    os << "  }\n";
    os << "  workgroupBarrier();\n";
    os << "  if (tid == 0u) {\n";
    os << "    var m: f32 = -3.402823e38;\n";
    os << "    for (var s: u32 = 0u; s < " << S << "u; s++) { m = max(m, scores[s]); }\n";
    os << "    var sm: f32 = 0.0;\n";
    os << "    for (var s: u32 = 0u; s < " << S << "u; s++) { sm += exp(scores[s] - m); }\n";
    os << "    maxv = m; sumv = sm;\n";
    os << "  }\n";
    os << "  workgroupBarrier();\n";
    os << "  for (var d: u32 = tid; d < " << D << "u; d += 128u) {\n";
    os << "    var acc: f32 = 0.0;\n";
    os << "    for (var s: u32 = 0u; s < " << S << "u; s++) { acc += exp(scores[s] - maxv) * v[vbase + s * " << D
       << "u + d]; }\n";
    os << "    o[qbase + d] = acc / sumv;\n";
    os << "  }\n";
    os << "}\n";
    out.pass1.clear();
    out.pass2      = os.str();
    out.groupsX2   = B * H;
    out.groupsY2   = T;
    out.inputCount = masked ? 4 : 3;
    return;
}


}  // namespace eve::tensor::wgsl_detail
