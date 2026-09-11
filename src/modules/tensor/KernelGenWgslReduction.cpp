#include <algorithm>
#include <cmath>
#include <sstream>
#include "common/Exception.h"
#include "tensor/KernelGenWgslInternal.h"
#include "tensor/Quant.h"

namespace eve::tensor::wgsl_detail {
void genSoftmax(const Graph &g, const FusedGroup &grp, KernelSpec &out) {
    const GraphNode &sn    = g.node(grp.outputNode);
    const GraphNode &X     = g.node(sn.in0);
    const int        axis  = sn.i0;
    int              outer = 1, reduce = 1, inner = 1;
    for (int k = 0; k < axis; ++k) outer *= X.dims[k];
    reduce = X.dims[axis];
    for (int k = axis + 1; k < X.rank; ++k) inner *= X.dims[k];
    const int  rows    = outer * inner;
    const bool logMode = grp.logMode;

    std::ostringstream os1, os2;
    os1 << header(kLocalSize);
    os1 << bufferDecl(0, "in_");
    os1 << bufferDecl(1, "mx");
    os1 << bufferDecl(2, "sm");
    os1 << pushConstant();
    os1 << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
           "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) "
           "{\n";
    os1 << "  var i_: u32 = globalId.x;\n";
    os1 << "  if (i_ >= " << rows << "u) { return; }\n";
    os1 << "  var o_: u32 = i_ / " << inner << "u;\n";
    os1 << "  var ii: u32 = i_ % " << inner << "u;\n";
    os1 << "  var m: f32 = -3.402823e38;\n";
    os1 << "  for (var j: u32 = 0u; j < " << reduce << "u; j++) {\n";
    os1 << "    m = max(m, in_[(o_ * " << reduce << "u + j) * " << inner << "u + ii]);\n";
    os1 << "  }\n";
    os1 << "  var s: f32 = 0.0;\n";
    os1 << "  for (var j: u32 = 0u; j < " << reduce << "u; j++) {\n";
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
    os2 << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
           "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) "
           "{\n";
    os2 << "  var i_: u32 = globalId.x;\n";
    os2 << "  if (i_ >= " << sn.size << "u) { return; }\n";
    os2 << "  var o_: u32 = (i_ / (" << reduce * inner << "u)) * " << inner << "u + (i_ % " << inner << "u);\n";
    os2 << "  var m: f32 = mx[o_];\n";
    os2 << "  var s: f32 = sm[o_];\n";
    os2 << "  var x: f32 = in_[i_];\n";
    if (logMode) {
        os2 << "  o[i_] = (x - m) - log(s);\n";
    } else {
        os2 << "  o[i_] = exp(x - m) / s;\n";
    }
    os2 << "}\n";
    out.pass1           = os1.str();
    out.pass2           = os2.str();
    out.groupsX1        = groupsFor(rows);
    out.groupsX2        = groupsFor(sn.size);
    out.inputCount      = 1;
    out.inputsReadPass1 = 1;
    out.statsCount      = 2;
    out.statsSize       = rows;
    out.twoPass         = true;
    return;
}

void genNorm(const Graph &g, const FusedGroup &grp, bool rms, KernelSpec &out) {
    const GraphNode &nn         = g.node(grp.outputNode);
    const GraphNode &X          = g.node(nn.in0);
    const int        cols       = X.dims[X.rank - 1];
    const int        rows       = X.size / cols;
    const float      eps        = nn.s0;
    const bool       hasScale   = grp.hasScale;
    const bool       hasBias    = !rms && grp.hasBias;
    const int        inputCount = 1 + (hasScale ? 1 : 0) + (hasBias ? 1 : 0);
    const int        statsCount = rms ? 1 : 2;
    const int        outBinding = inputCount + statsCount;

    std::ostringstream os1, os2;
    os1 << header(kLocalSize);
    os1 << bufferDecl(0, "in_");
    os1 << bufferDecl(1, "st0");
    if (statsCount > 1) os1 << bufferDecl(2, "st1");
    os1 << pushConstant();
    os1 << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
           "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) "
           "{\n";
    os1 << "  var i_: u32 = globalId.x;\n";
    os1 << "  if (i_ >= " << rows << "u) { return; }\n";
    os1 << "  var s0: f32 = 0.0;\n";
    if (statsCount > 1) os1 << "  var s1: f32 = 0.0;\n";
    os1 << "  for (var j: u32 = 0u; j < " << cols << "u; j++) {\n";
    os1 << "    var v: f32 = in_[i_ * " << cols << "u + j];\n";
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
    for (int k = 0; k < inputCount; ++k) os2 << bufferDecl(k, ("a" + std::to_string(k)).c_str());
    os2 << bufferDecl(inputCount, "st0");
    if (statsCount > 1) os2 << bufferDecl(inputCount + 1, "st1");
    os2 << bufferDecl(outBinding, "o");
    os2 << pushConstant();
    os2 << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
           "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) "
           "{\n";
    os2 << "  var i_: u32 = globalId.x;\n";
    os2 << "  if (i_ >= " << nn.size << "u) { return; }\n";
    os2 << "  var r: u32 = i_ / " << cols << "u;\n";
    os2 << "  var c: u32 = i_ % " << cols << "u;\n";
    if (rms) {
        os2 << "  var inv: f32 = 1.0 / sqrt(st0[r] / " << cols << ".0 + " << scalarStr(eps) << ");\n";
        os2 << "  var y: f32 = a0[i_] * inv;\n";
        if (hasScale) os2 << "  y *= a1[c];\n";
    } else {
        os2 << "  var mean: f32 = st0[r] / " << cols << ".0;\n";
        os2 << "  var variance: f32 = st1[r] / " << cols << ".0 - mean * mean;\n";
        os2 << "  variance = max(variance, 0.0);\n";
        os2 << "  var inv: f32 = 1.0 / sqrt(variance + " << scalarStr(eps) << ");\n";
        os2 << "  var y: f32 = (a0[i_] - mean) * inv;\n";
        if (hasScale) os2 << "  y *= a1[c];\n";
        if (hasBias) os2 << "  y += a" << (hasScale ? 2 : 1) << "[c];\n";
    }
    os2 << "  o[i_] = y;\n";
    os2 << "}\n";
    out.pass1           = os1.str();
    out.pass2           = os2.str();
    out.groupsX1        = groupsFor(rows);
    out.groupsX2        = groupsFor(nn.size);
    out.inputCount      = inputCount;
    out.inputsReadPass1 = 1;
    out.statsCount      = statsCount;
    out.statsSize       = rows;
    out.twoPass         = true;
    return;
}

void genReduceOrArgmax(const Graph &g, const FusedGroup &grp, bool argmax, KernelSpec &out) {
    const GraphNode &rn    = g.node(grp.outputNode);
    const GraphNode &X     = g.node(rn.in0);
    const int        axis  = rn.i0;
    int              outer = 1, reduce = 1, inner = 1;
    for (int k = 0; k < axis; ++k) outer *= X.dims[k];
    reduce = X.dims[axis];
    for (int k = axis + 1; k < X.rank; ++k) inner *= X.dims[k];
    const int          outSize = outer * inner;
    std::ostringstream os;
    os << header(kLocalSize);
    os << bufferDecl(0, "in_");
    os << bufferDecl(1, "o");
    os << pushConstant();
    os << "@compute @workgroup_size(workgroupX, workgroupY)\nfn main(@builtin(global_invocation_id) globalId: "
          "vec3<u32>, @builtin(local_invocation_id) localId: vec3<u32>, @builtin(workgroup_id) groupId: vec3<u32>) {\n";
    os << "  var i_: u32 = globalId.x;\n";
    os << "  if (i_ >= " << outSize << "u) { return; }\n";
    os << "  var o_: u32 = i_ / " << inner << "u;\n";
    os << "  var ii: u32 = i_ % " << inner << "u;\n";
    if (argmax) {
        os << "  var best: f32 = -3.402823e38;\n";
        os << "  var bestJ: f32 = 0.0;\n";
        os << "  for (var j: u32 = 0u; j < " << reduce << "u; j++) {\n";
        os << "    var v: f32 = in_[(o_ * " << reduce << "u + j) * " << inner << "u + ii];\n";
        os << "    if (v > best) { best = v; bestJ = f32(j); }\n";
        os << "  }\n";
        os << "  o[i_] = bestJ;\n";
    } else {
        switch (grp.op) {
            case OpType::ReduceSum:
            case OpType::ReduceMean:
                os << "  var acc: f32 = 0.0;\n";
                os << "  for (var j: u32 = 0u; j < " << reduce << "u; j++) { acc += in_[(o_ * " << reduce << "u + j) * "
                   << inner << "u + ii]; }\n";
                if (grp.op == OpType::ReduceMean) os << "  acc /= " << scalarStr(float(reduce)) << ";\n";
                break;
            case OpType::ReduceMin:
                os << "  var acc: f32 = 3.402823e38;\n";
                os << "  for (var j: u32 = 0u; j < " << reduce << "u; j++) { acc = min(acc, in_[(o_ * " << reduce
                   << "u + j) * " << inner << "u + ii]); }\n";
                break;
            case OpType::ReduceMax:
                os << "  var acc: f32 = -3.402823e38;\n";
                os << "  for (var j: u32 = 0u; j < " << reduce << "u; j++) { acc = max(acc, in_[(o_ * " << reduce
                   << "u + j) * " << inner << "u + ii]); }\n";
                break;
            default: throw eve::Exception("Tensor WGSL: unsupported kernel variant or binding count");
        }
        os << "  o[i_] = acc;\n";
    }
    os << "}\n";
    out.pass1.clear();
    out.pass2      = os.str();
    out.groupsX2   = groupsFor(outSize);
    out.inputCount = 1;
    return;
}


}  // namespace eve::tensor::wgsl_detail
