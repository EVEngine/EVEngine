#include "tensor/OnnxCompute.h"
#include "tensor/OnnxInternal.h"

#include <set>

namespace eve::tensor {
struct OnnxModel::Impl {
    onnx_detail::ModelData model;
};
OnnxModel::OnnxModel(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
OnnxModel::~OnnxModel() = default;
OnnxModelInfo                      OnnxModel::info() const { return impl_->model.info; }
Result<std::unique_ptr<OnnxModel>> OnnxModel::load(std::span<const uint8_t> bytes) {
    try {
        auto impl   = std::make_unique<Impl>();
        impl->model = onnx_detail::parse(bytes);
        return Result<std::unique_ptr<OnnxModel>>::success(std::unique_ptr<OnnxModel>(new OnnxModel(std::move(impl))));
    } catch (const onnx_detail::Failure& e) {
        return Result<std::unique_ptr<OnnxModel>>::failure(
            Diagnostic::error(e.code, e.what(), {}, {}, "tensor.onnx.import"));
    } catch (const std::exception& e) {
        return Result<std::unique_ptr<OnnxModel>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, e.what(), {}, {}, "tensor.onnx.import"));
    }
}
Result<std::vector<OnnxNamedTensor>> OnnxModel::run(std::span<const OnnxNamedTensor> feeds,
                                                    std::span<const std::string>     requested,
                                                    OnnxRunOptions                   options) const {
    return runInternal(feeds, requested, nullptr, options);
}
Result<OnnxGpuResult> OnnxModel::runGpu(std::span<const OnnxNamedTensor> feeds, OnnxCompute& compute,
                                        std::span<const std::string> requested, OnnxRunOptions options) const {
    class Counter final : public OnnxCompute {
    public:
        OnnxCompute& target;
        size_t       count = 0;
        explicit Counter(OnnxCompute& t) : target(t) {}
        Result<std::vector<uint8_t>> dispatch(const OnnxKernel& k) override {
            auto r = target.dispatch(k);
            if (r.ok()) ++count;
            return r;
        }
        Result<OnnxBuffer> enqueue(const std::string& source, std::span<const OnnxBuffer> inputs, size_t bytes,
                                   uint32_t work) override {
            auto r = target.enqueue(source, inputs, bytes, work);
            if (r.ok()) ++count;
            return r;
        }
    } counter(compute);
    compute.beginRun();
    struct RunScope {
        OnnxCompute& device;
        ~RunScope() { device.endRun(); }
    } scope{compute};
    auto r = runInternal(feeds, requested, &counter, options);
    if (!r.ok()) return Result<OnnxGpuResult>::failure(r.status());
    return Result<OnnxGpuResult>::success({std::move(r.value()), counter.count, compute.transferStats()});
}
Result<std::vector<OnnxNamedTensor>> OnnxModel::runInternal(std::span<const OnnxNamedTensor> feeds,
                                                            std::span<const std::string>     requested,
                                                            OnnxCompute* compute, OnnxRunOptions options) const {
    using namespace onnx_detail;
    std::string nodeName;
    try {
        const auto&                    model = impl_->model;
        const std::vector<std::string> targets =
            requested.empty() ? model.info.outputs : std::vector<std::string>(requested.begin(), requested.end());
        std::set<std::string> seenFeeds;
        for (const auto& f : feeds) {
            nodeName = f.name;
            if (count(f.tensor.shape) * elementSize(f.tensor.element) != f.tensor.bytes.size())
                throw Failure("Tensor byte count mismatch");
            auto input = model.inputs.find(f.name);
            if (input == model.inputs.end() || !seenFeeds.insert(f.name).second)
                throw Failure("Unknown or duplicate feed");
            if (static_cast<int>(f.tensor.element) != input->second.type ||
                f.tensor.shape.size() != input->second.shape.size())
                throw Failure("Feed type/rank mismatch");
            for (size_t i = 0; i < f.tensor.shape.size(); ++i)
                if (input->second.shape[i] >= 0 && input->second.shape[i] != f.tensor.shape[i])
                    throw Failure("Feed dimension mismatch");
        }
        nodeName.clear();
        auto result = evaluate(model, feeds, targets, compute, options);
        return Result<std::vector<OnnxNamedTensor>>::success(std::move(result));
    } catch (const Failure& e) {
        return Result<std::vector<OnnxNamedTensor>>::failure(
            Diagnostic::error(e.code, e.what(), e.path.empty() ? nodeName : e.path, {}, "tensor.onnx.run"));
    } catch (const std::exception& e) {
        return Result<std::vector<OnnxNamedTensor>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, e.what(), nodeName, {}, "tensor.onnx.run"));
    }
}
}  // namespace eve::tensor
