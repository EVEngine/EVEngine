#pragma once
#include "common/Export.h"

#include "dialogue/ConversationCompiler.h"

#include <unordered_map>

namespace eve::dialogue {

/** @brief Runtime translation and voice-production row keyed by stable key and locale. */
struct ConversationLocalizationEntry {
    std::string text;
    std::string voice;
    std::string status;
    double      duration = 0.0;
};

/** @brief CSV-backed translation and voice recording catalog with default-locale resolution. */
class EVENGINE_API_ORCHESTRATION ConversationLocalizationCatalog {
public:
    /** @brief Import csv. */
    int         importCsv(const std::string& csv, const std::string& defaultLocale,
                          std::vector<ConversationDiagnostic>& diagnostics);
    /** @brief Resolve text. */
    std::string resolveText(const std::string& key, const std::string& locale, const std::string& fallback) const;
    /** @brief Resolve voice. */
    std::string resolveVoice(const std::string& key, const std::string& locale, const std::string& fallback) const;
    /** @brief Resolve status. */
    std::string resolveStatus(const std::string& key, const std::string& locale) const;
    /** @brief Resolve duration. */
    double      resolveDuration(const std::string& key, const std::string& locale) const;
    /** @brief Export missing csv. */
    std::string exportMissingCsv(const std::vector<eve::dnut::SequenceAsset>& assets, const std::string& locale) const;
    /** @brief Export voice recording csv. */
    std::string exportVoiceRecordingCsv(const std::vector<eve::dnut::SequenceAsset>& assets,
                                        const std::string&                           locale) const;
    /** @brief Clears . */
    void        clear() { entries_.clear(); }

private:
    const ConversationLocalizationEntry* find(const std::string& key, const std::string& locale) const;
    std::unordered_map<std::string, ConversationLocalizationEntry> entries_;
};

}  // namespace eve::dialogue
