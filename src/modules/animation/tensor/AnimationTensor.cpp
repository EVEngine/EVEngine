#include "animation/tensor/AnimationTensor.h"

#include "animation/AnimSmrNeural.h"
#include "animation/tensor/SmrNeuralProvider.h"
#include "common/Capability.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::animation {

struct AnimationTensor::Impl {
    std::unique_ptr<SmrNeuralProvider> provider;
};

Module_IMPL(AnimationTensor, new AnimationTensor());

AnimationTensor::AnimationTensor() : impl_(std::make_unique<Impl>()) {
    impl_->provider = std::make_unique<SmrNeuralProvider>();
    cap::provide<ISmrNeuralRetarget>(impl_->provider.get());
}

AnimationTensor::~AnimationTensor() {
    if (impl_ && impl_->provider) cap::revoke<ISmrNeuralRetarget>(impl_->provider.get());
}

void AnimationTensor::expose(ssq::Table& table) {
    auto cls = table.addClass(name, AnimationTensor::create, false);
    expose(cls);
}

void AnimationTensor::expose(ssq::Class& cls) { cls.addFunc("getName", &AnimationTensor::getName); }

}  // namespace eve::animation
