#pragma once

#include "common/Module.h"
#include "common/Result.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::i18n {

/**
 * @brief Game localization (i18n) module.
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
    I18n()           = default;
    ~I18n() override = default;

    /**
     * @brief Atomically replace one locale from JSON with structured failure details.
     * @param lang Stable
     * locale identifier copied by the module.
     * @param json UTF-8 locale object copied and parsed synchronously.

     * * @return Applied on success; parse and argument failures do not mutate the locale.
     * @thread Main-thread
     * only. @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] eve::Result<void> replaceLocaleFromJson(const std::string& lang, const std::string& json);
    /**
     * @brief Atomically replace one locale from a VFS file with structured failure details.
     * @param lang
     * Stable locale identifier copied by the module.
     * @param path VFS path read synchronously and retained only
     * for hot-reload observation.
     * @return Applied on success; read and parse failures leave the locale
     * unchanged.
     * @thread Main-thread only. @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] eve::Result<void> replaceLocaleFromFile(const std::string& lang, const std::string& path);
    /** @brief Compatibility-only bool projection of replaceLocaleFromJson. */
    bool loadFromJson(const std::string& lang, const std::string& json);
    /** @brief Compatibility-only bool projection of replaceLocaleFromFile. */
    bool loadFromFile(const std::string& lang, const std::string& path);
    /**
     * @brief Atomically replace every locale from one strictly validated product bundle.
     * @param json
     * UTF-8 JSON using schema `eve.i18n.bundle`, version 1.
     * @return Number of admitted locales, or a path-scoped
     * diagnostic. Failure leaves the
     *         current locales, current language and default language unchanged.

     * * @ownership The module copies all admitted text; it does not retain `json`.
     * @thread Main-thread only.
     * Concurrent lookup or mutation is unsupported.
     * @reentrancy Does not invoke script or user callbacks.
 */
    [[nodiscard]] eve::Result<int> replaceBundleFromJson(const std::string& json);
    /** @brief 卸载 / 清空语言表。 */
    void unload(const std::string& lang);
    void clear();

    /**
     * @brief Select an admitted language with a structured missing-locale diagnostic.
     * @return Applied on
     * success; failure leaves the selected language unchanged.
     * @thread Main-thread only. @reentrancy Does not
     * invoke callbacks.
     */
    [[nodiscard]] eve::Result<void> selectLanguage(const std::string& lang);
    /** @brief Compatibility-only bool projection of selectLanguage. */
    bool        setLanguage(const std::string& lang);
    std::string getLanguage() const { return language_; }
    void        setDefaultLanguage(const std::string& lang) { defaultLanguage_ = lang; }
    std::string getDefaultLanguage() const { return defaultLanguage_; }
    int         getLanguageCount() const { return int(locales_.size()); }
    std::string getLanguageAt(int index) const;
    bool        hasLanguage(const std::string& lang) const;
    /** @brief Return whether one exact locale owns `key`, without default-language substitution. */
    bool hasInLanguage(const std::string& lang, const std::string& key) const;
    /**
     * @brief Validate that every admitted locale owns one exact product key.
     * @param key Dot-separated stable key whose segments use ASCII letters, digits, `_` or `-`.
     * @return Number of covered locales, or a locale/key-scoped diagnostic. Default-language substitution is never used.
     * @ownership The key is inspected synchronously and is not retained.
     * @thread Main-thread only; concurrent locale mutation is unsupported.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] eve::Result<int> validateKeyCoverage(const std::string& key) const;
    /** @brief Return the number of singular and plural keys owned by one locale, or zero if absent. */
    int getKeyCount(const std::string& lang) const;

    /** @brief 翻译查找：按键（点号命名空间）取字符串。 */
    bool        has(const std::string& key) const;
    std::string get(const std::string& key) const;
    std::string getWithParams(const std::string& key, const std::unordered_map<std::string, std::string>& params) const;
    std::string getPlural(const std::string& key, int n) const;
    std::string getPluralWithParams(const std::string& key, int n,
                                    const std::unordered_map<std::string, std::string>& params) const;

    // ---- hot reload (poll from the game loop) ----
    void setAutoReload(bool enable) { autoReload_ = enable; }
    bool isAutoReload() const { return autoReload_; }
    /** @brief Re-read changed locale files; returns the number of reloads performed. */
    int update(float dt);

private:
    struct Locale {
        std::unordered_map<std::string, std::string> strings;  // flat key -> value
        std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
                    plurals;  // key -> {form -> value}
        std::string path;     // empty if loaded from raw json
        int64_t     modtime = -1;
    };

    /** @brief Borrow a locale until the next mutation. @ownership Borrowed. @lifetime Invalidated by locale mutation.
     */
    Locale* findLocale(const std::string& lang);
    /** @brief Borrow a locale until the next mutation. @ownership Borrowed. @lifetime Invalidated by locale mutation.
     */
    const Locale* findLocale(const std::string& lang) const;

    std::string formatString(const std::string& tpl, const std::unordered_map<std::string, std::string>& params) const;
    std::string pluralForm(const std::string& lang, int n) const;

    std::unordered_map<std::string, Locale> locales_;
    std::string                             language_        = "en";
    std::string                             defaultLanguage_ = "en";
    bool                                    autoReload_      = true;
};

}  // namespace eve::i18n
