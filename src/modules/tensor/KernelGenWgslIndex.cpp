#include <algorithm>
#include <cmath>
#include <sstream>
#include "common/Exception.h"
#include "tensor/KernelGenWgslInternal.h"
#include "tensor/Quant.h"

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
