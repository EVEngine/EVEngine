#include "i18n/I18n.h"

#include "common/Json.h"
#include "common/Module.h"
#include "common/SquirrelBinding.h"
#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <sstream>
#include <unordered_set>
#include <vector>

namespace eve::i18n {

Module_IMPL(I18n, new I18n());

namespace {

bool isPluralCategory(const std::string& key) {
    static const char* cats[] = {"zero", "one", "two", "few", "many", "other"};
    for (const char* c : cats)
        if (key == c) return true;
    return false;
}

/** Default ostream formatting, so script floats interpolate like JSON numbers. */
std::string numberToString(double v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

template <typename T>
eve::Result<T> bundleFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "i18n.bundle"));
}

bool validIdentifier(const std::string& value) {
    if (value.empty() || value.size() > 64) return false;
    for (const unsigned char ch : value)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' ||
              ch == '_'))
            return false;
    return true;
}

bool collectPlaceholders(const std::string& text, std::set<std::string>& result, std::string& error) {
    result.clear();
    for (size_t offset = 0; offset < text.size();) {
        if (text[offset] == '}') {
            error = "unmatched closing placeholder brace";
            return false;
        }
        if (text[offset] != '{') {
            ++offset;
            continue;
        }
        const size_t close = text.find('}', offset + 1);
        if (close == std::string::npos) {
            error = "unterminated placeholder";
            return false;
        }
        const std::string name = text.substr(offset + 1, close - offset - 1);
        if (!validIdentifier(name)) {
            error = "placeholder names must be stable identifiers";
            return false;
        }
        result.insert(name);
        offset = close + 1;
    }
    return true;
}

bool flattenStrict(eve::json::Value node, const std::string& prefix, int depth,
                   std::unordered_map<std::string, std::string>&                                  strings,
                   std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& plurals,
                   std::string& error, std::string& errorPath) {
    if (depth > 16) {
        error     = "locale nesting exceeds 16 levels";
        errorPath = prefix;
        return false;
    }
    if (!node.isObject() || node.size() == 0) {
        error     = "translation namespaces must be non-empty objects";
        errorPath = prefix;
        return false;
    }
    const std::vector<std::string> names     = node.keys();
    bool                           allPlural = true;
    bool                           anyPlural = false;
    for (const auto& name : names) {
        if (isPluralCategory(name))
            anyPlural = true;
        else
            allPlural = false;
        if (!validIdentifier(name)) {
            error     = "translation key segments must be stable identifiers";
            errorPath = prefix + "." + name;
            return false;
        }
    }
    if (anyPlural && !allPlural) {
        error     = "plural categories cannot be mixed with namespace keys";
        errorPath = prefix;
        return false;
    }
    if (allPlural) {
        if (prefix.empty() || !node.has("other")) {
            error     = "plural tables require a non-empty key and an other form";
            errorPath = prefix;
            return false;
        }
        std::unordered_map<std::string, std::string> forms;
        for (const auto& name : names) {
            const auto value = node.get(name.c_str());
            if (!value.isString() || value.asString().empty() || value.asString().size() > 16384) {
                error     = "plural forms must be non-empty strings of at most 16384 bytes";
                errorPath = prefix + "." + name;
                return false;
            }
            forms.emplace(name, value.asString());
        }
        plurals.emplace(prefix, std::move(forms));
        return true;
    }
    for (const auto& name : names) {
        const auto        value = node.get(name.c_str());
        const std::string path  = prefix.empty() ? name : prefix + "." + name;
        if (value.isString()) {
            if (value.asString().empty() || value.asString().size() > 16384) {
                error     = "translations must be non-empty strings of at most 16384 bytes";
                errorPath = path;
                return false;
            }
            strings.emplace(path, value.asString());
        } else if (value.isObject()) {
            if (!flattenStrict(value, path, depth + 1, strings, plurals, error, errorPath)) return false;
        } else {
            error     = "translations must be strings, namespaces, or plural tables";
            errorPath = path;
            return false;
        }
        if (strings.size() + plurals.size() > 10000) {
            error     = "a locale may contain at most 10000 translation keys";
            errorPath = path;
            return false;
        }
    }
    return true;
}

void flatten(eve::json::Value node, const std::string& prefix, std::unordered_map<std::string, std::string>& strings,
             std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& plurals) {
    if (node.isObject()) {
        const std::vector<std::string> names = node.keys();
        // A plain object of plural categories is a plural form table.
        bool allCats    = !names.empty();
        bool allStrings = true;
        for (const auto& k : names) {
            if (!isPluralCategory(k)) {
                allCats = false;
                break;
            }
            if (!node.get(k.c_str()).isString()) allStrings = false;
        }
        if (allCats && allStrings && !prefix.empty()) {
            std::unordered_map<std::string, std::string> forms;
            for (const auto& k : names) forms[k] = node.getString(k.c_str());
            plurals[prefix] = std::move(forms);
            return;
        }
        for (const auto& k : names) {
            const std::string path = prefix.empty() ? k : prefix + "." + k;
            flatten(node.get(k.c_str()), path, strings, plurals);
        }
        return;
    }
    if (prefix.empty()) return;
    // Scalars stringify; arrays and nulls are not translatable and are skipped.
    if (node.isString() || node.isNumber() || node.isBool()) strings[prefix] = node.asString();
}

bool parseLocale(const std::string& text, std::unordered_map<std::string, std::string>& strings,
                 std::unordered_map<std::string, std::unordered_map<std::string, std::string>>& plurals,
                 std::string*                                                                   error) {
    const eve::json::Document doc = eve::json::Document::parse(text, error);
    if (!doc.valid()) return false;
    if (!doc.root().isObject()) {
        if (error) *error = "locale root must be a JSON object";
        return false;
    }
    strings.clear();
    plurals.clear();
    flatten(doc.root(), "", strings, plurals);
    return true;
}

int64_t fileModtime(const std::string& path) {
    auto* fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    eve::filesystem::Filesystem::Info info{};
    if (!fs->getInfo(path, info)) return -1;
    return info.modtime;
}

// Convert a Squirrel table (or null) into a string params map.
std::unordered_map<std::string, std::string> readParams(ssq::Object params) {
    std::unordered_map<std::string, std::string> out;
    HSQUIRRELVM                                  vm = params.getHandle();
    if (!vm) return out;
    const HSQOBJECT raw = params.getRaw();
    if (raw._type != OT_TABLE) return out;

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, raw);
    sq_pushnull(vm);  // iterator
    while (SQ_SUCCEEDED(sq_next(vm, -2))) {
        // key at -2, value at -1
        const SQChar* k = nullptr;
        if (sq_gettype(vm, -2) == OT_STRING && SQ_SUCCEEDED(sq_getstring(vm, -2, &k)) && k) {
            std::string        v;
            const SQObjectType vt = sq_gettype(vm, -1);
            if (vt == OT_STRING) {
                const SQChar* sv = nullptr;
                if (SQ_SUCCEEDED(sq_getstring(vm, -1, &sv)) && sv) v = sv;
            } else if (vt == OT_INTEGER) {
                SQInteger iv = 0;
                if (SQ_SUCCEEDED(sq_getinteger(vm, -1, &iv))) v = std::to_string(iv);
            } else if (vt == OT_FLOAT) {
                SQFloat fv = 0;
                if (SQ_SUCCEEDED(sq_getfloat(vm, -1, &fv))) v = numberToString(fv);
            } else if (vt == OT_BOOL) {
                SQBool bv = SQFalse;
                if (SQ_SUCCEEDED(sq_getbool(vm, -1, &bv))) v = bv ? "true" : "false";
            }
            out[k] = std::move(v);
        }
        sq_pop(vm, 2);
    }
    sq_settop(vm, top);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Module implementation
// ---------------------------------------------------------------------------

I18n::Locale* I18n::findLocale(const std::string& lang) {
    auto it = locales_.find(lang);
    return it == locales_.end() ? nullptr : &it->second;
}

const I18n::Locale* I18n::findLocale(const std::string& lang) const {
    auto it = locales_.find(lang);
    return it == locales_.end() ? nullptr : &it->second;
}

eve::Result<void> I18n::replaceLocaleFromJson(const std::string& lang, const std::string& json) {
    if (lang.empty())
        return bundleFailure<void>(eve::DiagnosticCode::InvalidArgument, "locale identifier must not be empty",
                                   "locale");
    Locale      loc;
    std::string error;
    if (!parseLocale(json, loc.strings, loc.plurals, &error))
        return bundleFailure<void>(eve::DiagnosticCode::ParseError, error.empty() ? "invalid locale JSON" : error,
                                   "locale." + lang);
    loc.path       = "";
    loc.modtime    = -1;
    locales_[lang] = std::move(loc);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool I18n::loadFromJson(const std::string& lang, const std::string& json) {
    return replaceLocaleFromJson(lang, json).ok();
}

eve::Result<int> I18n::replaceBundleFromJson(const std::string& json) {
    std::string parseError;
    const auto  document = eve::json::Document::parse(json, &parseError);
    if (!document.valid())
        return bundleFailure<int>(eve::DiagnosticCode::ParseError,
                                  parseError.empty() ? "invalid localization bundle JSON" : parseError, "$");
    const auto root = document.root();
    if (!root.isObject())
        return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument, "localization bundle root must be an object",
                                  "$");
    const std::unordered_set<std::string> rootFields = {"schema", "version", "defaultLocale", "locales"};
    for (const auto& field : root.keys())
        if (!rootFields.contains(field))
            return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                      "localization bundle contains an unknown field", "$." + field);
    const auto schema        = root.get("schema");
    const auto version       = root.get("version");
    const auto defaultLocale = root.get("defaultLocale");
    const auto locales       = root.get("locales");
    if (!schema.isString() || schema.asString() != "eve.i18n.bundle")
        return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument, "localization bundle schema id is invalid",
                                  "$.schema");
    if (!version.isInt64() || version.asInt64() != 1)
        return bundleFailure<int>(eve::DiagnosticCode::UnknownVersion, "unsupported localization bundle schema version",
                                  "$.version");
    if (!defaultLocale.isString() || !validIdentifier(defaultLocale.asString()))
        return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                  "defaultLocale must be a stable locale identifier", "$.defaultLocale");
    if (!locales.isObject() || locales.size() == 0 || locales.size() > 32)
        return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                  "locales must contain between 1 and 32 locale objects", "$.locales");
    if (!locales.has(defaultLocale.asString().c_str()))
        return bundleFailure<int>(eve::DiagnosticCode::NotFound, "defaultLocale is not present in locales",
                                  "$.defaultLocale");

    std::unordered_map<std::string, Locale> proposed;
    for (const auto& lang : locales.keys()) {
        if (!validIdentifier(lang))
            return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument, "locale names must be stable identifiers",
                                      "$.locales." + lang);
        Locale      candidate;
        std::string error;
        std::string errorPath;
        if (!flattenStrict(locales.get(lang.c_str()), "", 0, candidate.strings, candidate.plurals, error, errorPath))
            return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument, std::move(error),
                                      "$.locales." + lang + (errorPath.empty() ? "" : "." + errorPath));
        proposed.emplace(lang, std::move(candidate));
    }

    const Locale& source = proposed.at(defaultLocale.asString());
    for (const auto& [lang, locale] : proposed) {
        for (const auto& [key, sourceText] : source.strings) {
            const auto translated = locale.strings.find(key);
            if (translated == locale.strings.end())
                return bundleFailure<int>(eve::DiagnosticCode::NotFound,
                                          "locale is missing a required singular translation",
                                          "$.locales." + lang + "." + key);
            std::set<std::string> expected;
            std::set<std::string> actual;
            std::string           placeholderError;
            if (!collectPlaceholders(sourceText, expected, placeholderError))
                return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument, placeholderError,
                                          "$.locales." + defaultLocale.asString() + "." + key);
            if (!collectPlaceholders(translated->second, actual, placeholderError))
                return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument, placeholderError,
                                          "$.locales." + lang + "." + key);
            if (expected != actual)
                return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                          "translation placeholders do not match the default locale",
                                          "$.locales." + lang + "." + key);
        }
        for (const auto& [key, translated] : locale.strings)
            if (!source.strings.contains(key))
                return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                          "locale contains a singular key absent from the default locale",
                                          "$.locales." + lang + "." + key);
        for (const auto& [key, sourceForms] : source.plurals) {
            const auto translated = locale.plurals.find(key);
            if (translated == locale.plurals.end())
                return bundleFailure<int>(eve::DiagnosticCode::NotFound,
                                          "locale is missing a required plural translation",
                                          "$.locales." + lang + "." + key);
            for (const auto& [form, translatedText] : translated->second) {
                const auto         exactSource    = sourceForms.find(form);
                const auto         fallbackSource = sourceForms.find("other");
                const std::string& sourceText =
                    exactSource != sourceForms.end() ? exactSource->second : fallbackSource->second;
                std::set<std::string> expected;
                std::set<std::string> actual;
                std::string           placeholderError;
                if (!collectPlaceholders(sourceText, expected, placeholderError))
                    return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument, placeholderError,
                                              "$.locales." + defaultLocale.asString() + "." + key + "." + form);
                if (!collectPlaceholders(translatedText, actual, placeholderError))
                    return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument, placeholderError,
                                              "$.locales." + lang + "." + key + "." + form);
                if (expected != actual)
                    return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                              "plural placeholders do not match the default locale",
                                              "$.locales." + lang + "." + key + "." + form);
            }
        }
        for (const auto& [key, forms] : locale.plurals) {
            (void)forms;
            if (!source.plurals.contains(key))
                return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                          "locale contains a plural key absent from the default locale",
                                          "$.locales." + lang + "." + key);
        }
    }

    const std::string nextDefault  = defaultLocale.asString();
    const std::string nextLanguage = proposed.contains(language_) ? language_ : nextDefault;
    locales_                       = std::move(proposed);
    defaultLanguage_               = nextDefault;
    language_                      = nextLanguage;
    return eve::Result<int>::success(static_cast<int>(locales_.size()), eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> I18n::replaceLocaleFromFile(const std::string& lang, const std::string& path) {
    if (lang.empty() || path.empty())
        return bundleFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                   "locale identifier and VFS path must not be empty", "locale.file");

    auto* fs = ModuleManager::getInstance<filesystem::Filesystem>("Filesystem");
    if (!fs) fs = filesystem::Filesystem::create();

    filesystem::FileData* fd = nullptr;
    try {
        fd = fs->read(path);
    } catch (...) {
        delete fd;
        return bundleFailure<void>(eve::DiagnosticCode::NotFound, "locale file could not be read", path);
    }
    if (fd == nullptr || fd->getData() == nullptr || fd->getSize() == 0) {
        delete fd;
        return bundleFailure<void>(eve::DiagnosticCode::NotFound, "locale file is missing or empty", path);
    }

    std::string text(static_cast<const char*>(fd->getData()), fd->getSize());
    delete fd;

    Locale      loc;
    std::string error;
    if (!parseLocale(text, loc.strings, loc.plurals, &error))
        return bundleFailure<void>(eve::DiagnosticCode::ParseError, error.empty() ? "invalid locale JSON" : error,
                                   path);

    loc.path    = path;
    loc.modtime = fileModtime(path);
    fs->watch(path);
    locales_[lang] = std::move(loc);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool I18n::loadFromFile(const std::string& lang, const std::string& path) {
    return replaceLocaleFromFile(lang, path).ok();
}

void I18n::unload(const std::string& lang) { locales_.erase(lang); }

void I18n::clear() { locales_.clear(); }

eve::Result<void> I18n::selectLanguage(const std::string& lang) {
    if (!hasLanguage(lang))
        return bundleFailure<void>(eve::DiagnosticCode::NotFound, "language has not been admitted", "language." + lang);
    language_ = lang;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

bool I18n::setLanguage(const std::string& lang) { return selectLanguage(lang).ok(); }

std::string I18n::getLanguageAt(int index) const {
    if (index < 0 || size_t(index) >= locales_.size()) return {};
    std::vector<std::string> langs;
    langs.reserve(locales_.size());
    for (const auto& [lang, loc] : locales_) langs.push_back(lang);
    std::sort(langs.begin(), langs.end());
    return langs[size_t(index)];
}

bool I18n::hasLanguage(const std::string& lang) const { return findLocale(lang) != nullptr; }

bool I18n::hasInLanguage(const std::string& lang, const std::string& key) const {
    const Locale* locale = findLocale(lang);
    return locale && (locale->strings.contains(key) || locale->plurals.contains(key));
}

eve::Result<int> I18n::validateKeyCoverage(const std::string& key) const {
    if (key.empty() || key.back() == '.')
        return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument, "translation key must not be empty",
                                  "requiredKey");
    size_t offset = 0;
    while (offset < key.size()) {
        const size_t      separator = key.find('.', offset);
        const std::string segment   = key.substr(offset, separator == std::string::npos ? std::string::npos
                                                                                       : separator - offset);
        if (!validIdentifier(segment))
            return bundleFailure<int>(eve::DiagnosticCode::InvalidArgument,
                                      "translation key segments must be stable identifiers", "requiredKey." + key);
        if (separator == std::string::npos) break;
        offset = separator + 1;
    }
    if (locales_.empty())
        return bundleFailure<int>(eve::DiagnosticCode::NotFound, "no locales have been admitted", "locales");
    std::vector<std::string> languages;
    languages.reserve(locales_.size());
    for (const auto& [language, locale] : locales_) languages.push_back(language);
    std::sort(languages.begin(), languages.end());
    for (const auto& language : languages)
        if (!hasInLanguage(language, key))
            return bundleFailure<int>(eve::DiagnosticCode::NotFound, "required translation key is missing",
                                      "locales." + language + "." + key);
    return eve::Result<int>::success(static_cast<int>(languages.size()));
}

int I18n::getKeyCount(const std::string& lang) const {
    const Locale* locale = findLocale(lang);
    return locale ? static_cast<int>(locale->strings.size() + locale->plurals.size()) : 0;
}

bool I18n::has(const std::string& key) const {
    const Locale* cur = findLocale(language_);
    if (cur && (cur->strings.find(key) != cur->strings.end() || cur->plurals.find(key) != cur->plurals.end()))
        return true;
    if (defaultLanguage_ != language_) {
        if (const Locale* def = findLocale(defaultLanguage_))
            if (def->strings.find(key) != def->strings.end() || def->plurals.find(key) != def->plurals.end())
                return true;
    }
    return false;
}

std::string I18n::get(const std::string& key) const {
    if (const Locale* cur = findLocale(language_)) {
        auto it = cur->strings.find(key);
        if (it != cur->strings.end()) return it->second;
    }
    if (defaultLanguage_ != language_) {
        if (const Locale* def = findLocale(defaultLanguage_)) {
            auto it = def->strings.find(key);
            if (it != def->strings.end()) return it->second;
        }
    }
    return key;
}

std::string I18n::formatString(const std::string&                                  tpl,
                               const std::unordered_map<std::string, std::string>& params) const {
    std::string out;
    out.reserve(tpl.size());
    for (size_t i = 0; i < tpl.size();) {
        if (tpl[i] == '{') {
            const size_t close = tpl.find('}', i + 1);
            if (close != std::string::npos) {
                const std::string key = tpl.substr(i + 1, close - i - 1);
                const auto        it  = params.find(key);
                if (it != params.end()) {
                    out += it->second;
                    i = close + 1;
                    continue;
                }
            }
        }
        out += tpl[i++];
    }
    return out;
}

std::string I18n::getWithParams(const std::string&                                  key,
                                const std::unordered_map<std::string, std::string>& params) const {
    return formatString(get(key), params);
}

std::string I18n::getPlural(const std::string& key, int n) const { return getPluralWithParams(key, n, {}); }

std::string I18n::getPluralWithParams(const std::string& key, int n,
                                      const std::unordered_map<std::string, std::string>& params) const {
    std::unordered_map<std::string, std::string> merged = params;
    merged["n"]                                         = std::to_string(n);

    const Locale* cur = findLocale(language_);
    const Locale* def = (defaultLanguage_ != language_) ? findLocale(defaultLanguage_) : nullptr;
    if (!cur && !def) return key;

    // Look up in the current language first, then fall back to the default.
    std::string current;
    if (const Locale* loc = cur) {
        const auto pit = loc->plurals.find(key);
        if (pit != loc->plurals.end()) {
            const std::string form = pluralForm(language_, n);
            auto              it   = pit->second.find(form);
            if (it == pit->second.end()) it = pit->second.find("other");
            if (it == pit->second.end()) it = pit->second.find("one");
            if (it != pit->second.end()) return formatString(it->second, merged);
        } else {
            const auto sit = loc->strings.find(key);
            if (sit != loc->strings.end()) return formatString(sit->second, merged);
        }
    }
    if (const Locale* loc = def) {
        const auto pit = loc->plurals.find(key);
        if (pit != loc->plurals.end()) {
            const std::string form = pluralForm(defaultLanguage_, n);
            auto              it   = pit->second.find(form);
            if (it == pit->second.end()) it = pit->second.find("other");
            if (it == pit->second.end()) it = pit->second.find("one");
            if (it != pit->second.end()) return formatString(it->second, merged);
        } else {
            const auto sit = loc->strings.find(key);
            if (sit != loc->strings.end()) return formatString(sit->second, merged);
        }
    }
    return key;
}

std::string I18n::pluralForm(const std::string& lang, int n) const {
    std::string base = lang;
    if (const size_t dash = base.find('-'); dash != std::string::npos) base = base.substr(0, dash);
    if (const size_t under = base.find('_'); under != std::string::npos) base = base.substr(0, under);

    if (base == "zh" || base == "ja" || base == "ko" || base == "th" || base == "vi" || base == "id" || base == "ms" ||
        base == "tr" || base == "my")
        return "other";

    if (base == "fr" || base == "pt") return (n == 0 || n == 1) ? "one" : "other";

    if (base == "ru" || base == "uk" || base == "be") {
        const int mod10  = n % 10;
        const int mod100 = n % 100;
        if (mod10 == 1 && mod100 != 11) return "one";
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return "few";
        return "many";
    }

    if (base == "pl") {
        const int mod10  = n % 10;
        const int mod100 = n % 100;
        if (n == 1) return "one";
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return "few";
        return "many";
    }

    if (base == "cs" || base == "sk") {
        if (n == 1) return "one";
        if (n >= 2 && n <= 4) return "few";
        return "other";
    }

    return (n == 1) ? "one" : "other";
}

int I18n::update(float dt) {
    (void)dt;
    if (!autoReload_) return 0;

    // Collect dirty entries first so reloading cannot invalidate the iterator.
    std::vector<std::string> dirty;
    for (const auto& [lang, loc] : locales_) {
        if (loc.path.empty()) continue;
        const int64_t mt = fileModtime(loc.path);
        if (mt >= 0 && mt != loc.modtime) dirty.push_back(lang);
    }

    int reloaded = 0;
    for (const std::string& lang : dirty) {
        if (replaceLocaleFromFile(lang, locales_[lang].path).ok()) ++reloaded;
    }
    return reloaded;
}

// ---------------------------------------------------------------------------
// Script binding
// ---------------------------------------------------------------------------

void I18n::expose(ssq::Table& table) {
    auto cls = table.addClass(name, I18n::create, false);
    expose(cls);
}

void I18n::expose(ssq::Class& cls) {
    cls.addFunc("getName", &I18n::getName);
    cls.addFunc("loadFromJson", &I18n::loadFromJson);
    cls.addFunc("loadFromFile", &I18n::loadFromFile);
    cls.addFunc("replaceLocaleFromJson", [vm = cls.getHandle()](I18n* self, const std::string& lang,
                                                                const std::string& json) {
        if (!self)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::InvalidArgument,
                                                      "I18n receiver must not be null", "i18n", {}, "i18n.squirrel")));
        return eve::script::projectResult(vm, self->replaceLocaleFromJson(lang, json));
    });
    cls.addFunc("replaceLocaleFromFile", [vm = cls.getHandle()](I18n* self, const std::string& lang,
                                                                const std::string& path) {
        if (!self)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::InvalidArgument,
                                                      "I18n receiver must not be null", "i18n", {}, "i18n.squirrel")));
        return eve::script::projectResult(vm, self->replaceLocaleFromFile(lang, path));
    });
    cls.addFunc("replaceBundleFromJson", [vm = cls.getHandle()](I18n* self, const std::string& json) {
        if (!self)
            return eve::script::projectResult(vm,
                                              eve::Result<int>::failure(eve::Diagnostic::error(
                                                  eve::DiagnosticCode::InvalidArgument,
                                                  "I18n receiver must not be null", "i18n", {}, "i18n.squirrel")),
                                              [](int count) { return eve::Value(count); });
        return eve::script::projectResult(vm, self->replaceBundleFromJson(json),
                                          [](int count) { return eve::Value(count); });
    });
    cls.addFunc("unload", &I18n::unload);
    cls.addFunc("clear", &I18n::clear);
    cls.addFunc("setLanguage", &I18n::setLanguage);
    cls.addFunc("selectLanguage", [vm = cls.getHandle()](I18n* self, const std::string& lang) {
        if (!self)
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::InvalidArgument,
                                                      "I18n receiver must not be null", "i18n", {}, "i18n.squirrel")));
        return eve::script::projectResult(vm, self->selectLanguage(lang));
    });
    cls.addFunc("getLanguage", &I18n::getLanguage);
    cls.addFunc("setDefaultLanguage", &I18n::setDefaultLanguage);
    cls.addFunc("getDefaultLanguage", &I18n::getDefaultLanguage);
    cls.addFunc("getLanguageCount", &I18n::getLanguageCount);
    cls.addFunc("getLanguageAt", &I18n::getLanguageAt);
    cls.addFunc("hasLanguage", &I18n::hasLanguage);
    cls.addFunc("hasInLanguage", &I18n::hasInLanguage);
    cls.addFunc("validateKeyCoverage", [vm = cls.getHandle()](I18n* self, const std::string& key) {
        if (!self)
            return eve::script::projectResult(vm,
                                              eve::Result<int>::failure(eve::Diagnostic::error(
                                                  eve::DiagnosticCode::InvalidArgument,
                                                  "I18n receiver must not be null", "i18n", {}, "i18n.squirrel")),
                                              [](int count) { return eve::Value(count); });
        return eve::script::projectResult(vm, self->validateKeyCoverage(key),
                                          [](int count) { return eve::Value(count); });
    });
    cls.addFunc("getKeyCount", &I18n::getKeyCount);
    cls.addFunc("has", &I18n::has);
    cls.addFunc("get", &I18n::get);
    cls.addFunc("getWithParams", [](I18n* self, const std::string& key, ssq::Object params) -> std::string {
        return self->getWithParams(key, readParams(params));
    });
    cls.addFunc("getPlural", &I18n::getPlural);
    cls.addFunc("getPluralWithParams",
                [](I18n* self, const std::string& key, int n, ssq::Object params) -> std::string {
                    return self->getPluralWithParams(key, n, readParams(params));
                });
    cls.addFunc("setAutoReload", &I18n::setAutoReload);
    cls.addFunc("isAutoReload", &I18n::isAutoReload);
    cls.addFunc("update", &I18n::update);
}

}  // namespace eve::i18n
