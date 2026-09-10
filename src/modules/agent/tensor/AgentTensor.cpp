#include "agent/tensor/AgentTensor.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "agent/Agent.h"
#include "agent/tensor/TensorBackend.h"
#include "common/Capability.h"

namespace eve::agent {
struct AgentTensor::Impl {
    std::unique_ptr<IPolicyBackend>    provider;
    std::unique_ptr<IGpuPolicyBackend> gpu;
};
Module_IMPL(AgentTensor, new AgentTensor());
AgentTensor::AgentTensor() : impl_(std::make_unique<Impl>()) {
    impl_->provider = makeTensorBackend().expect("Tensor backend creation failed");
    impl_->gpu      = makeGpuBackend().expect("GPU provider creation failed");
    cap::provide<IPolicyBackend>(impl_->provider.get());
    cap::provide<IGpuPolicyBackend>(impl_->gpu.get());
}
AgentTensor::~AgentTensor() {
    cap::revoke<IGpuPolicyBackend>(impl_->gpu.get());
    cap::revoke<IPolicyBackend>(impl_->provider.get());
}
void AgentTensor::expose(ssq::Table& table) {
    auto cls = table.addClass(name, AgentTensor::create, false);
    expose(cls);
}
void AgentTensor::expose(ssq::Class& cls) { cls.addFunc("getName", &AgentTensor::getName); }
}  // namespace eve::agent
