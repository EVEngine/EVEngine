#include "editor/EditorLocalization.h"

#include <set>
#include <utility>

namespace eve::editor {
namespace {

template <class T>
EditorResult<T> localizationError(EditorStatus status, const char* rule, std::string message) {
    return EditorResult<T>::error(status, RuleId(rule), std::move(message));
}

std::set<std::string> placeholders(const std::string& text) {
    std::set<std::string> result;
    for (size_t start = text.find('{'); start != std::string::npos; start = text.find('{', start + 1)) {
        const size_t end = text.find('}', start + 1);
        if (end == std::string::npos) break;
        const std::string name = text.substr(start + 1, end - start - 1);
        if (!name.empty()) result.insert(name);
        start = end;
    }
    return result;
}

void issue(LocalizationAnalysis& result, const char* rule, DiagnosticSeverity severity,
           const std::string& key, const std::string& locale, const std::string& message) {
    result.diagnostics.push_back({RuleId(rule), severity,
                                  message + " [key=" + key + ", locale=" + locale + "]"});
    if (severity == DiagnosticSeverity::Error) result.status = EditorStatus::Failed;
}

const EditorValue* field(const EditorValue& value, const char* key) {
    const auto* object = value.getIf<EditorValue::Object>();
    if (!object) return nullptr;
    const auto found = object->find(key);
    return found == object->end() ? nullptr : &found->second;
}

}  // namespace

EditorResult<void> LocalizationDocument::addRow(std::string key, std::string sourceText,
                                                 std::string context) {
    if (key.empty() || sourceText.empty())
        return localizationError<void>(EditorStatus::Rejected, "editor.localization.invalid-source",
                                       "Localization key and source text are required");
    if (rows_.contains(key))
        return localizationError<void>(EditorStatus::Conflict, "editor.localization.duplicate-key",
                                       "Localization key already exists: " + key);
    LocalizationRow row;
    row.key = key;
    row.context = std::move(context);
    row.sourceText = std::move(sourceText);
    rows_.emplace(std::move(key), std::move(row));
    ++revision_;
    return EditorResult<void>::applied();
}

EditorResult<void> LocalizationDocument::removeRow(const std::string& key) {
    if (rows_.erase(key) == 0)
        return localizationError<void>(EditorStatus::NotFound, "editor.localization.key-not-found",
                                       "Localization key was not found: " + key);
    ++revision_;
    return EditorResult<void>::applied();
}

EditorResult<void> LocalizationDocument::setVariant(const std::string& key, std::string locale,
                                                     LocalizationVariant variant) {
    auto row = rows_.find(key);
    if (row == rows_.end())
        return localizationError<void>(EditorStatus::NotFound, "editor.localization.key-not-found",
                                       "Localization key was not found: " + key);
    if (locale.empty() || variant.voiceDuration < 0.0)
        return localizationError<void>(EditorStatus::Rejected, "editor.localization.invalid-variant",
                                       "Locale is required and voice duration cannot be negative");
    static const std::set<std::string> statuses{"", "missing", "recording", "review", "approved"};
    if (!statuses.contains(variant.voiceStatus))
        return localizationError<void>(EditorStatus::Rejected, "editor.localization.invalid-voice-status",
                                       "Unknown voice production status: " + variant.voiceStatus);
    row->second.variants[std::move(locale)] = std::move(variant);
    ++revision_;
    return EditorResult<void>::applied();
}

std::vector<LocalizationRow> LocalizationDocument::rows() const {
    std::vector<LocalizationRow> result;
    result.reserve(rows_.size());
    for (const auto& [key, row] : rows_) {
        static_cast<void>(key);
        result.push_back(row);
    }
    return result;
}

LocalizationAnalysis LocalizationDocument::analyze(const std::vector<std::string>& requiredLocales,
                                                    bool requireVoice) const {
    LocalizationAnalysis result;
    std::set<std::string> uniqueLocales;
    for (const std::string& locale : requiredLocales) {
        if (locale.empty() || !uniqueLocales.insert(locale).second) {
            issue(result, "editor.localization.invalid-locale", DiagnosticSeverity::Error, "", locale,
                  "Required locales must be unique and non-empty");
            continue;
        }
        LocalizationLocaleCoverage coverage;
        coverage.locale = locale;
        coverage.total = static_cast<int>(rows_.size());
        for (const auto& [key, row] : rows_) {
            const auto found = row.variants.find(locale);
            if (found == row.variants.end() || found->second.text.empty()) {
                issue(result, "editor.localization.missing-translation", DiagnosticSeverity::Error, key, locale,
                      "Required translation is missing");
                continue;
            }
            const LocalizationVariant& variant = found->second;
            ++coverage.translated;
            if (placeholders(row.sourceText) != placeholders(variant.text))
                issue(result, "editor.localization.placeholder-mismatch", DiagnosticSeverity::Error, key, locale,
                      "Translation placeholders do not match source text");
            if (!variant.voiceAsset.empty()) ++coverage.voiced;
            if (variant.voiceStatus == "approved") ++coverage.approved;
            if (variant.voiceAsset.empty() && variant.voiceDuration > 0.0)
                issue(result, "editor.localization.orphan-voice-duration", DiagnosticSeverity::Warning, key, locale,
                      "Voice duration exists without a voice asset");
            if (!variant.voiceAsset.empty() && variant.voiceDuration <= 0.0)
                issue(result, "editor.localization.missing-voice-duration", DiagnosticSeverity::Warning, key, locale,
                      "Voice asset has no measured duration");
            if (requireVoice && variant.voiceAsset.empty())
                issue(result, "editor.localization.missing-voice", DiagnosticSeverity::Error, key, locale,
                      "Required localized voice asset is missing");
        }
        result.locales.push_back(std::move(coverage));
    }
    return result;
}

EditorValue LocalizationDocument::snapshotValue() const {
    EditorValue::Array rows;
    for (const auto& [key, row] : rows_) {
        EditorValue::Object variants;
        for (const auto& [locale, variant] : row.variants)
            variants[locale] = EditorValue::Object{{"text", variant.text},
                                                    {"voiceAsset", variant.voiceAsset},
                                                    {"voiceStatus", variant.voiceStatus},
                                                    {"voiceDuration", variant.voiceDuration}};
        rows.emplace_back(EditorValue::Object{{"key", key}, {"context", row.context},
                                              {"sourceText", row.sourceText},
                                              {"variants", std::move(variants)}});
    }
    return EditorValue::Object{{"schemaVersion", int64_t{1}}, {"rows", std::move(rows)}};
}

EditorResult<void> LocalizationDocument::loadSnapshot(const EditorValue& snapshot) {
    const auto* schema = field(snapshot, "schemaVersion");
    const auto* rowsValue = field(snapshot, "rows");
    const auto* version = schema ? schema->getIf<int64_t>() : nullptr;
    const auto* rows = rowsValue ? rowsValue->getIf<EditorValue::Array>() : nullptr;
    if (!version || *version != 1 || !rows)
        return localizationError<void>(EditorStatus::Unsupported, "editor.localization.invalid-snapshot",
                                       "Localization snapshot schema is unsupported");
    LocalizationDocument candidate;
    for (const EditorValue& rowValue : *rows) {
        const auto* keyValue = field(rowValue, "key");
        const auto* sourceValue = field(rowValue, "sourceText");
        const auto* contextValue = field(rowValue, "context");
        const auto* variantsValue = field(rowValue, "variants");
        const auto* key = keyValue ? keyValue->getIf<std::string>() : nullptr;
        const auto* source = sourceValue ? sourceValue->getIf<std::string>() : nullptr;
        const auto* context = contextValue ? contextValue->getIf<std::string>() : nullptr;
        const auto* variants = variantsValue ? variantsValue->getIf<EditorValue::Object>() : nullptr;
        if (!key || !source || !context || !variants || !candidate.addRow(*key, *source, *context).accepted())
            return localizationError<void>(EditorStatus::Rejected, "editor.localization.invalid-row",
                                           "Localization snapshot contains an invalid source row");
        for (const auto& [locale, variantValue] : *variants) {
            const auto* textValue = field(variantValue, "text");
            const auto* assetValue = field(variantValue, "voiceAsset");
            const auto* statusValue = field(variantValue, "voiceStatus");
            const auto* durationValue = field(variantValue, "voiceDuration");
            const auto* text = textValue ? textValue->getIf<std::string>() : nullptr;
            const auto* asset = assetValue ? assetValue->getIf<std::string>() : nullptr;
            const auto* status = statusValue ? statusValue->getIf<std::string>() : nullptr;
            const auto* duration = durationValue ? durationValue->getIf<double>() : nullptr;
            if (!text || !asset || !status || !duration ||
                !candidate.setVariant(*key, locale, {*text, *asset, *status, *duration}).accepted())
                return localizationError<void>(EditorStatus::Rejected, "editor.localization.invalid-variant",
                                               "Localization snapshot contains an invalid locale variant");
        }
    }
    rows_ = std::move(candidate.rows_);
    ++revision_;
    return EditorResult<void>::applied();
}

}  // namespace eve::editor
