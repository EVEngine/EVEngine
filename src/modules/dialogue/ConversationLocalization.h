#pragma once

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

/** @brief CSV-backed translation and voice recording catalog with locale fallback. */
class ConversationLocalizationCatalog {
public:
    int         importCsv(const std::string& csv, const std::string& defaultLocale,
                          std::vector<ConversationDiagnostic>& diagnostics);
    std::string resolveText(const std::string& key, const std::string& locale, const std::string& fallback) const;
    std::string resolveVoice(const std::string& key, const std::string& locale, const std::string& fallback) const;
    std::string resolveStatus(const std::string& key, const std::string& locale) const;
    double      resolveDuration(const std::string& key, const std::string& locale) const;
    std::string exportMissingCsv(const std::vector<ConversationAsset>& assets, const std::string& locale) const;
    std::string exportVoiceRecordingCsv(const std::vector<ConversationAsset>& assets, const std::string& locale) const;
    void        clear() { entries_.clear(); }

private:
    const ConversationLocalizationEntry* find(const std::string& key, const std::string& locale) const;
    std::unordered_map<std::string, ConversationLocalizationEntry> entries_;
};

}  // namespace eve::dialogue
