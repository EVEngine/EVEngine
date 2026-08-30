#include "audio_editing/AudioEditingProvider.h"

#include <utility>

namespace eve::audio_editing {
namespace {
class AudioEditingFactory final : public IAudioEditingFactory {
public:
    std::unique_ptr<AudioSourcePublishingTarget> createSource(
        std::string id, IAudioSourceRuntimeSink* sink) const override {
        return std::make_unique<AudioSourcePublishingTarget>(std::move(id), sink);
    }
};
}  // namespace

editing::Result<editing::ProviderHandle> registerEditingProvider(
    editing::ExtensionProviderRegistry& registry) {
    editing::ExtensionDescriptor descriptor;
    descriptor.id              = "audio.editing";
    descriptor.schemaVersion   = 1;
    descriptor.requiredModules = {"audio"};
    return editing::registerStaticProvider(registry, std::move(descriptor),
                                           IAudioEditingFactory::capabilityId(),
                                           std::make_shared<AudioEditingFactory>());
}

}  // namespace eve::audio_editing
