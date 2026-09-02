#include "localization_editing/LocalizationDocument.h"

#include "audio/Source.h"
#include "dialogue/DialogueVoice.h"

#include <algorithm>
#include <utility>

namespace eve::localization_editing {
namespace {

EditorResult<void> auditionError(EditorStatus status, const char* rule, std::string message) {
    return eve::editing::failed<void>(status, RuleId(rule), std::move(message));
}

}  // namespace

EditorResult<void> LocalizationVoiceAudition::play(const LocalizationDocument& document,
                                                    const std::string& key,
                                                    const std::string& locale,
                                                    dialogue::DialogueVoice* voice,
                                                    const SourceResolver& sources) const {
    if (!voice || !sources || key.empty() || locale.empty())
        return auditionError(EditorStatus::Rejected, "editor.localization.invalid-audition-input",
                             "Voice audition requires key, locale, voice service and source resolver");
    const auto rows = document.rows();
    const auto row = std::find_if(rows.begin(), rows.end(),
                                  [&](const LocalizationRow& item) { return item.key == key; });
    if (row == rows.end())
        return auditionError(EditorStatus::NotFound, "editor.localization.key-not-found",
                             "Localization key was not found: " + key);
    const auto variant = row->variants.find(locale);
    if (variant == row->variants.end() || variant->second.voiceAsset.empty())
        return auditionError(EditorStatus::NotFound, "editor.localization.voice-not-found",
                             "Localized voice asset is missing: " + key + " / " + locale);
    audio::Source* source = sources(variant->second.voiceAsset);
    if (!source)
        return auditionError(EditorStatus::NotFound, "editor.localization.voice-asset-unresolved",
                             "Voice asset could not be resolved: " + variant->second.voiceAsset);
    voice->stop();
    if (!voice->bindSource(key, locale, source) || !voice->play(key, locale))
        return auditionError(EditorStatus::Failed, "editor.localization.voice-playback-failed",
                             "Voice source could not be registered or played");
    return eve::editing::applied<void>();
}

}  // namespace eve::localization_editing
