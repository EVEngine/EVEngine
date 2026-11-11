#include "audio/AudioAmbient.h"

#include "audio/Source.h"

namespace eve::audio {

Result<void> configureAmbientBed(Source* source) {
    if (!source)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "configureAmbientBed requires a source", "audio.ambient"));
    auto blend = source->setSpatialBlend(0.f);
    if (!blend.ok()) return blend;
    auto balance = source->setBalance(0.f);
    if (!balance.ok()) {
        // Mono relative beds accept balance=0; stereo without angles still gets blend=0.
        // Balance failure at 0 on unsupported formats is non-fatal for bed setup when
        // the default OpenAL center image already applies — only fail stereo without
        // angles if the caller later requests a non-zero pan via setBalance.
        if (source->getChannelCount() == 2) return Result<void>::success();
        return balance;
    }
    return Result<void>::success();
}

}  // namespace eve::audio
