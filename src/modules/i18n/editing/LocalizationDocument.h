#pragma once

#include "editing/EditingProtocol.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace eve::audio {
class Source;
}
namespace eve::dialogue {
class DialogueVoice;
}

namespace eve::localization_editing {
using DiagnosticSeverity=editing::DiagnosticSeverity; using EditorDiagnostic=editing::Diagnostic;
template<class T>using EditorResult=editing::Result<T>; using EditorStatus=editing::Status;
using EditorValue=editing::Value; using Revision=editing::Revision; using RuleId=editing::RuleId;

/** @brief One locale's editable translation and voice-production metadata. */
struct LocalizationVariant {
    std::string text;
    std::string voiceAsset;
    std::string voiceStatus;
    double voiceDuration = 0.0;
};

/** @brief One stable localization key and all locale variants. */
struct LocalizationRow {
    std::string key;
    std::string context;
    std::string sourceText;
    std::map<std::string, LocalizationVariant> variants;
};

/** @brief Aggregate production coverage for one locale. */
struct LocalizationLocaleCoverage {
    std::string locale;
    int total = 0;
    int translated = 0;
    int voiced = 0;
    int approved = 0;
};

/** @brief Completeness analysis result with key/locale-scoped diagnostics. */
struct LocalizationAnalysis {
    EditorStatus status = EditorStatus::Applied;
    std::vector<LocalizationLocaleCoverage> locales;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief UI-neutral localization table with deterministic persistence and QA. */
class LocalizationDocument {
public:
    /** @brief Insert a unique source key. */
    EditorResult<void> addRow(std::string key, std::string sourceText, std::string context = {});
    /** @brief Remove a source key and all translated variants. */
    EditorResult<void> removeRow(const std::string& key);
    /** @brief Replace one locale variant, validating production metadata. */
    EditorResult<void> setVariant(const std::string& key, std::string locale,
                                  LocalizationVariant variant);
    /** @brief Return rows in deterministic key order. */
    std::vector<LocalizationRow> rows() const;
    /** @brief Analyze locale coverage, placeholders and optional voice requirements. */
    LocalizationAnalysis analyze(const std::vector<std::string>& requiredLocales,
                                 bool requireVoice = false) const;
    /** @brief Capture deterministic data for conflict-safe document persistence. */
    EditorValue snapshotValue() const;
    /** @brief Atomically replace data from a validated persisted snapshot. */
    EditorResult<void> loadSnapshot(const EditorValue& snapshot);
    /** @brief Return the current edit revision. */
    Revision revision() const { return revision_; }

private:
    std::map<std::string, LocalizationRow> rows_;
    Revision revision_ = 0;
};

/** @brief Optional bridge registering and playing localized voice through DialogueVoice. */
class LocalizationVoiceAudition {
public:
    using SourceResolver = std::function<audio::Source*(const std::string& asset)>;
    /** @brief Resolve, register and start a locale voice clip without changing the document. */
    EditorResult<void> play(const LocalizationDocument& document, const std::string& key,
                            const std::string& locale, dialogue::DialogueVoice* voice,
                            const SourceResolver& sources) const;
};

}  // namespace eve::localization_editing
