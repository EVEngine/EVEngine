#include <sstream>
#include "tensor/KernelGenInternal.h"

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
