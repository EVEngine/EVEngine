#pragma once

#include "common/Module.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::i18n {

/**
 * Game localization (i18n) module.
 *
 * Locale tables map a translation key to a string (or a set of plural forms).
 * Keys use dot notation for namespacing, e.g. `menu.start` resolves the JSON
 * path `menu -> start`. Lookup falls back to the default language and finally
 * to the raw key so untranslated keys stay visible during development.
 *
 * Locale JSON shape:
 * @code
 * {
 *   "menu": { "start": "Start", "quit": "Quit" },
 *   "items": { "one": "{n} item", "other": "{n} items" },
 *   "greeting": "Hello, {name}!"
 * }
 * @endcode
 *
 * An object whose keys are all plural categories (zero/one/two/few/many/other)
 * is treated as a plural form table for its key. `{name}` placeholders are
 * substituted from the params table; `{n}` is filled by getPlural.
 *
 * Script: `i18n <- eve.I18n();`
 */
class I18n : public Module {
public:
    Module_REG(I18n);
    I18n() = default;
    ~I18n() override = default;

    // ---- loading ----
    bool loadFromJson(const std::string &lang, const std::string &json);
    bool loadFromFile(const std::string &lang, const std::string &path);
    void unload(const std::string &lang);
    void clear();

    // ---- language management ----
    bool setLanguage(const std::string &lang);
    std::string getLanguage() const { return language_; }
    void setDefaultLanguage(const std::string &lang) { defaultLanguage_ = lang; }
    std::string getDefaultLanguage() const { return defaultLanguage_; }
    int getLanguageCount() const { return int(locales_.size()); }
    std::string getLanguageAt(int index) const;
    bool hasLanguage(const std::string &lang) const;

    // ---- lookup ----
    bool has(const std::string &key) const;
    std::string get(const std::string &key) const;
    std::string getWithParams(const std::string &key,
                              const std::unordered_map<std::string, std::string> &params) const;
    std::string getPlural(const std::string &key, int n) const;
    std::string getPluralWithParams(const std::string &key, int n,
                                    const std::unordered_map<std::string, std::string> &params) const;

    // ---- hot reload (poll from the game loop) ----
    void setAutoReload(bool enable) { autoReload_ = enable; }
    bool isAutoReload() const { return autoReload_; }
    /** Re-read changed locale files; returns the number of reloads performed. */
    int update(float dt);

private:
    struct Locale {
        std::unordered_map<std::string, std::string> strings;    // flat key -> value
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
            plurals;                                            // key -> {form -> value}
        std::string path;                                       // empty if loaded from raw json
        int64_t     modtime = -1;
    };

    Locale *      findLocale(const std::string &lang);
    const Locale *findLocale(const std::string &lang) const;

    std::string formatString(const std::string &tpl,
                             const std::unordered_map<std::string, std::string> &params) const;
    std::string pluralForm(const std::string &lang, int n) const;

    std::unordered_map<std::string, Locale> locales_;
    std::string language_        = "en";
    std::string defaultLanguage_ = "en";
    bool        autoReload_      = true;
};

}  // namespace eve::i18n