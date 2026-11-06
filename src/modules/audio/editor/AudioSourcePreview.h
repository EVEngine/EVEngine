#pragma once
#include "common/Export.h"

#include <memory>
#include "audio/editing/AudioWaveform.h"

namespace eve::editing {
class TaskContext;
}
namespace eve::audio_editor {
/**
 * @brief Immutable waveform provider, retained by each submitted job across replacement.
 * @thread Worker only; inputs are owned by the job and borrowed for the call. No editor access or callbacks.
 * @reentrancy Shared providers can be called concurrently by different sessions; mutable provider state must be
 * synchronized.
 * @note Final shared-provider release may run on the owner or worker thread; provider destruction must support both.
 * @lifetime Provider must bound its computation and check cancellation; destruction may wait for the call.
 */
class EVENGINE_API_EDITORS IAudioSourcePreviewProvider {
public:
    virtual ~IAudioSourcePreviewProvider() = default;
    /** @brief Compute a waveform from an immutable PCM snapshot; use TaskContext to cancel cooperatively.
     * @cost Linear in decoded samples plus output buckets, once per invalidation.
     */
    [[nodiscard]] virtual audio_editing::Result<audio_editing::AudioWaveformResult> generate(
        const audio_editing::AudioWaveformRequest& request, const audio_editing::EditorAudioPcm& pcm,
        const editing::TaskContext& context) const = 0;
};
/** @brief Create the native CPU waveform provider; this does not attach audio output. */
[[nodiscard]] EVENGINE_API_EDITORS std::shared_ptr<const IAudioSourcePreviewProvider> makeAudioSourcePreviewProvider();
}  // namespace eve::audio_editor
