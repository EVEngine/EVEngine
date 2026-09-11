#include "tensor/OnnxGpuKernels.h"
#include <sstream>
#include "tensor/OnnxInternal.h"
namespace eve::tensor::onnx_detail {
namespace {
std::string prefix(bool as, bool bs) {
    std::ostringstream s;
    s << "#version 450\nlayout(local_size_x=64) in;\n"
         "layout(std430,binding=0) readonly buffer A {uint a[];};\n"
         "layout(std430,binding=1) readonly buffer B {uint b[];};\n"
         "layout(std430,binding=2) readonly buffer Z {int zp[];};\n"
         "layout(std430,binding=3) writeonly buffer Y {int y[];};\n"
         "int av(uint i){int v=int((a[i/4]>>((i%4)*8))&255u);return "
      << (as ? "v>=128?v-256:v" : "v")
      << ";}\n"
         "int bv(uint i){int v=int((b[i/4]>>((i%4)*8))&255u);return "
      << (bs ? "v>=128?v-256:v" : "v") << ";}\n";
    return s.str();
}
// Compatibility host-byte calls use the same resident kernel generators. The
// adapter below requests completion through the provider's synchronous contract.
class Synchronous final : public OnnxCompute {
    OnnxCompute& target;

public:
    explicit Synchronous(OnnxCompute& t) : target(t) {}
    Result<std::vector<uint8_t>> dispatch(const OnnxKernel& k) override { return target.dispatch(k); }
};
OnnxBuffer hostBuffer(std::span<const uint8_t> bytes) {
    auto p = std::make_shared<const std::vector<uint8_t>>(bytes.begin(), bytes.end());
    return {p, {}, p->size()};
}
std::vector<int32_t> integerOutput(const OnnxBuffer& result) {
    if (!result.host || result.host->size() % 4) throw Failure("Invalid synchronous integer output");
    std::vector<int32_t> out(result.host->size() / 4);
    std::memcpy(out.data(), result.host->data(), result.host->size());
    return out;
}
}  // namespace
std::vector<int32_t> gpuMatmul(OnnxCompute& d, affine::ByteView a, affine::ByteView b, size_t m, size_t k, size_t n,
                               int az, std::span<const int32_t> bz) {
    Synchronous sync(d);
    return integerOutput(gpuMatmulResident(sync, hostBuffer(a.bytes), hostBuffer(b.bytes), a.signedValues,
                                           b.signedValues, m, k, n, az, bz));
}
std::vector<int32_t> gpuConv(OnnxCompute& d, affine::ByteView x, affine::ByteView w, const affine::ConvShape& c, int xz,
                             std::span<const int32_t> wz) {
    Synchronous sync(d);
    return integerOutput(
        gpuConvResident(sync, hostBuffer(x.bytes), hostBuffer(w.bytes), x.signedValues, w.signedValues, c, xz, wz));
}
OnnxBuffer enqueueInteger(OnnxCompute& d, const std::string& source, OnnxBuffer a, OnnxBuffer b,
                          std::span<const int32_t> z, size_t size, size_t terms) {
    if (terms > INT32_MAX / 65025 || !size || size > 64u * 65535u)
        throw Failure("GPU integer kernel exceeds exact accumulator or dispatch limit", DiagnosticCode::Unsupported);
    std::vector<uint8_t> bytes(z.size_bytes());
    std::memcpy(bytes.data(), z.data(), bytes.size());
    auto                          host = std::make_shared<const std::vector<uint8_t>>(std::move(bytes));
    const std::vector<OnnxBuffer> inputs{a, b, {host, {}, host->size()}};
    auto                          r = d.enqueue(source, inputs, size * 4, static_cast<uint32_t>(size));
    if (!r.ok()) throw Failure(r.error()->message(), r.error()->code());
    return std::move(r.value());
}
OnnxBuffer gpuMatmulResident(OnnxCompute& d, OnnxBuffer a, OnnxBuffer b, bool as, bool bs, size_t m, size_t k, size_t n,
                             int az, std::span<const int32_t> bz, size_t bOffset) {
    if (bz.size() != 1 && bz.size() != n) throw Failure("GPU matmul zero-point extent mismatch");
    std::ostringstream s;
    s << prefix(as, bs) << "void main(){uint i=gl_GlobalInvocationID.x;if(i>=" << m * n << "u)return;"
      << "uint r=i/" << n << "u,c=i%" << n << "u;int v=0;for(uint j=0;j<" << k << "u;++j)"
      << "v+=(av(r*" << k << "u+j)-(" << az << "))*(bv(" << bOffset << "u+j*" << n << "u+c)-zp["
      << (bz.size() == 1 ? "0" : "c") << "]);y[i]=v;}";
    return enqueueInteger(d, s.str(), a, b, bz, m * n, k);
}

OnnxBuffer gpuConvResident(OnnxCompute& d, OnnxBuffer x, OnnxBuffer w, bool xs, bool ws, const affine::ConvShape& c,
                           int xz, std::span<const int32_t> wz) {
    const int64_t oh =
        (int64_t(c.height) + c.padTop + c.padBottom - int64_t(c.dilationH) * (c.kernelH - 1) - 1) / c.strideH + 1;
    const int64_t ow =
        (int64_t(c.width) + c.padLeft + c.padRight - int64_t(c.dilationW) * (c.kernelW - 1) - 1) / c.strideW + 1;
    if ((oh - 1) * c.strideH > INT32_MAX || (ow - 1) * c.strideW > INT32_MAX ||
        (oh - 1) * c.strideH - c.padTop + int64_t(c.kernelH - 1) * c.dilationH > INT32_MAX ||
        (ow - 1) * c.strideW - c.padLeft + int64_t(c.kernelW - 1) * c.dilationW > INT32_MAX)
        throw Failure("GPU convolution coordinate overflow", DiagnosticCode::Unsupported);
    const size_t       size = size_t(c.batch) * c.outputs * oh * ow;
    const int          cg   = c.channels / c.groups;
    std::ostringstream s;
    s << prefix(xs, ws) << "void main(){uint i=gl_GlobalInvocationID.x;if(i>=" << size << "u)return;"
      << "int xx=int(i%" << ow << "u),yy=int(i/" << ow << "u%" << oh << "u),o=int(i/" << ow * oh << "u%" << c.outputs
      << "u),batch=int(i/" << ow * oh * c.outputs << "u);int v=0;"
      << "for(int ch=0;ch<" << cg << ";++ch)for(int ky=0;ky<" << c.kernelH << ";++ky)for(int kx=0;kx<" << c.kernelW
      << ";++kx){"
      << "int iy=yy*" << c.strideH << "-" << c.padTop << "+ky*" << c.dilationH << ",ix=xx*" << c.strideW << "-"
      << c.padLeft << "+kx*" << c.dilationW << ";"
      << "if(iy<0||ix<0||iy>=" << c.height << "||ix>=" << c.width << ")continue;"
      << "uint ai=uint(((batch*" << c.channels << "+o/" << c.outputs / c.groups << "*" << cg << "+ch)*" << c.height
      << "+iy)*" << c.width << "+ix);"
      << "uint bi=uint(((o*" << cg << "+ch)*" << c.kernelH << "+ky)*" << c.kernelW << "+kx);"
      << "v+=(av(ai)-(" << xz << "))*(bv(bi)-zp[" << (wz.size() == 1 ? "0" : "o") << "]);}y[i]=v;}";
    return enqueueInteger(d, s.str(), x, w, wz, size, size_t(cg) * c.kernelH * c.kernelW);
}
}  // namespace eve::tensor::onnx_detail
