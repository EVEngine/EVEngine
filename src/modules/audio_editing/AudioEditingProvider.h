#pragma once

#include "audio_editing/AudioTarget.h"
#include "editing/EditingExtension.h"

#include <memory>
#include <string>

namespace eve::audio_editing {

/** @brief Open capability for constructing audio authoring targets without editor type switches. */
class IAudioEditingFactory {
public:
    virtual ~IAudioEditingFactory() = default;
    /**
     * @brief Create an independently owned publishing target.
     * @param id Stable target identity.
     * @param sink Borrowed runtime sink that must outlive the returned target; null disables publication.
     * @return Newly allocated target owned by the caller.
     * @thread Main-thread only.
     */
    [[nodiscard]] virtual std::unique_ptr<AudioSourcePublishingTarget> createSource(
        std::string id, IAudioSourceRuntimeSink* sink) const = 0;
    /** @brief Return the stable capability id used with ProviderLease::query(). */
    static editing::CapabilityId capabilityId() { return editing::CapabilityId("audio.editing.factory"); }
};

/**
 * @brief Publish the audio editing factory into a host-owned extension registry.
 * @param registry Host-owned registry that must outlive the provider handle and leases.
 * @return Generation-qualified provider handle or a structured registration failure.
 * @thread Main-thread registration path; acquired leases follow the descriptor affinity.
 */
[[nodiscard]] editing::Result<editing::ProviderHandle> registerEditingProvider(
    editing::ExtensionProviderRegistry& registry);

}  // namespace eve::audio_editing
