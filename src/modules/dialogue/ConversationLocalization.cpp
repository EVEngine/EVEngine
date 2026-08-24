#include "dialogue/ConversationLocalization.h"

#include <charconv>

namespace eve::dialogue {
namespace {

using CsvRows = std::vector<std::vector<std::string>>;

std::string trim(std::string value) {
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) value.pop_back();
    size_t begin = 0;
    while (begin < value.size() && value[begin] == ' ') ++begin;
    return value.substr(begin);
}

bool parseCsv(const std::string& source, CsvRows& rows, std::string& error) {
    std::vector<std::string> row;
    std::string              field;
    bool                     quoted = false;
    for (size_t i = 0; i <= source.size(); ++i) {
        const char ch = i < source.size() ? source[i] : '\n';
        if (quoted) {
            if (ch == '"' && i + 1 < source.size() && source[i + 1] == '"') {
                field.push_back('"');
                ++i;
            } else if (ch == '"')
                quoted = false;
            else
                field.push_back(ch);
        } else if (ch == '"' && field.empty())
            quoted = true;
        else if (ch == ',') {
            row.push_back(std::move(field));
            field.clear();
        } else if (ch == '\n') {
            row.push_back(trim(std::move(field)));
            field.clear();
            if (!(row.size() == 1 && row.front().empty())) rows.push_back(std::move(row));
            row.clear();
        } else if (ch != '\r')
            field.push_back(ch);
    }
    if (quoted) {
        error = "unterminated quoted CSV field";
        return false;
    }
    if (!rows.empty() && !rows[0].empty() && rows[0][0].rfind("\xEF\xBB\xBF", 0) == 0) rows[0][0].erase(0, 3);
    return true;
}

std::string quote(std::string value) {
    size_t position = 0;
    while ((position = value.find('"', position)) != std::string::npos) {
        value.insert(position, 1, '"');
        position += 2;
    }
    return '"' + value + '"';
}

std::string stableKey(const ConversationAsset& asset, const ConversationAsset::Node& node) {
    return node.i18nKey.empty() ? asset.id + "." + node.id : node.i18nKey;
}

std::string mapKey(const std::string& key, const std::string& locale) { return locale + '\x1f' + key; }

}  // namespace

int ConversationLocalizationCatalog::importCsv(const std::string& csv, const std::string& defaultLocale,
                                               std::vector<ConversationDiagnostic>& diagnostics) {
    CsvRows     rows;
    std::string error;
    if (!parseCsv(csv, rows, error) || rows.empty()) {
        diagnostics.push_back({ConversationDiagnostic::Severity::Error, "<localization.csv>", 0,
                               error.empty() ? "localization CSV is empty" : error});
        return 0;
    }
    std::unordered_map<std::string, size_t> columns;
    for (size_t i = 0; i < rows.front().size(); ++i) columns[rows.front()[i]] = i;
    if (!columns.count("i18n_key")) {
        diagnostics.push_back({ConversationDiagnostic::Severity::Error, "<localization.csv>", 1,
                               "localization CSV requires an i18n_key column"});
        return 0;
    }
    const auto cell = [&](const std::vector<std::string>& row, const char* name) -> std::string {
        const auto it = columns.find(name);
        return it != columns.end() && it->second < row.size() ? row[it->second] : std::string{};
    };
    int imported = 0;
    for (size_t line = 1; line < rows.size(); ++line) {
        const std::string key    = cell(rows[line], "i18n_key");
        std::string       locale = cell(rows[line], "locale");
        if (locale.empty()) locale = defaultLocale;
        if (key.empty()) {
            diagnostics.push_back({ConversationDiagnostic::Severity::Warning, "<localization.csv>",
                                   static_cast<int>(line + 1), "row has no i18n_key and was ignored"});
            continue;
        }
        auto&       entry = entries_[mapKey(key, locale)];
        std::string text  = cell(rows[line], "translation");
        if (text.empty()) text = cell(rows[line], "text");
        if (!text.empty()) entry.text = std::move(text);
        const std::string voice = cell(rows[line], "voice");
        if (!voice.empty()) entry.voice = voice;
        const std::string status = cell(rows[line], "status");
        if (!status.empty()) entry.status = status;
        const std::string duration = cell(rows[line], "duration");
        if (!duration.empty()) {
            double     value  = 0.0;
            const auto result = std::from_chars(duration.data(), duration.data() + duration.size(), value);
            if (result.ec == std::errc{})
                entry.duration = value;
            else
                diagnostics.push_back({ConversationDiagnostic::Severity::Warning, "<localization.csv>",
                                       static_cast<int>(line + 1), "invalid duration was ignored"});
        }
        ++imported;
    }
    return imported;
}

const ConversationLocalizationEntry* ConversationLocalizationCatalog::find(const std::string& key,
                                                                           const std::string& locale) const {
    const auto exact = entries_.find(mapKey(key, locale));
    if (exact != entries_.end()) return &exact->second;
    const size_t separator = locale.find_first_of("-_");
    if (separator != std::string::npos) {
        const auto language = entries_.find(mapKey(key, locale.substr(0, separator)));
        if (language != entries_.end()) return &language->second;
    }
    const auto fallback = entries_.find(mapKey(key, {}));
    return fallback == entries_.end() ? nullptr : &fallback->second;
}

std::string ConversationLocalizationCatalog::resolveText(const std::string& key, const std::string& locale,
                                                         const std::string& fallback) const {
    const auto* entry = find(key, locale);
    return entry && !entry->text.empty() ? entry->text : fallback;
}

std::string ConversationLocalizationCatalog::resolveVoice(const std::string& key, const std::string& locale,
                                                          const std::string& fallback) const {
    const auto* entry = find(key, locale);
    return entry && !entry->voice.empty() ? entry->voice : fallback;
}

std::string ConversationLocalizationCatalog::resolveStatus(const std::string& key, const std::string& locale) const {
    const auto* entry = find(key, locale);
    return entry ? entry->status : std::string{};
}

double ConversationLocalizationCatalog::resolveDuration(const std::string& key, const std::string& locale) const {
    const auto* entry = find(key, locale);
    return entry ? entry->duration : 0.0;
}

std::string ConversationLocalizationCatalog::exportMissingCsv(const std::vector<ConversationAsset>& assets,
                                                              const std::string&                    locale) const {
    std::string out = "conversation_id,node_id,i18n_key,locale,source_text,translation\r\n";
    for (const auto& asset : assets)
        for (const auto& node : asset.nodes) {
            if (node.kind != ConversationAsset::Node::Kind::Line) continue;
            const std::string key   = stableKey(asset, node);
            const auto*       entry = find(key, locale);
            if (entry && !entry->text.empty()) continue;
            out += quote(asset.id) + ',' + quote(node.id) + ',' + quote(key) + ',' + quote(locale) + ',' +
                   quote(node.text) + ",\"\"\r\n";
        }
    return out;
}

std::string ConversationLocalizationCatalog::exportVoiceRecordingCsv(const std::vector<ConversationAsset>& assets,
                                                                     const std::string& locale) const {
    std::string out =
        "conversation_id,node_id,i18n_key,locale,speaker,source_text,translation,voice,status,duration\r\n";
    for (const auto& asset : assets)
        for (const auto& node : asset.nodes) {
            if (node.kind != ConversationAsset::Node::Kind::Line) continue;
            const std::string key   = stableKey(asset, node);
            const auto*       entry = find(key, locale);
            out += quote(asset.id) + ',' + quote(node.id) + ',' + quote(key) + ',' + quote(locale) + ',' +
                   quote(node.speaker) + ',' + quote(node.text) + ',' + quote(entry ? entry->text : std::string{}) +
                   ',' + quote(entry && !entry->voice.empty() ? entry->voice : node.voice) + ',' +
                   quote(entry ? entry->status : std::string{}) + ',' +
                   quote(entry && entry->duration > 0.0 ? std::to_string(entry->duration) : std::string{}) + "\r\n";
        }
    return out;
}

}  // namespace eve::dialogue
