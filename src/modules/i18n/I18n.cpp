#include "i18n/I18n.h"

#include "filesystem/FileData.h"
#include "filesystem/Filesystem.h"
#include "common/Json.h"
#include "common/Module.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <vector>

namespace eve::i18n {

Module_IMPL(I18n, new I18n());

namespace {

bool isPluralCategory(const std::string &key) {
    static const char *cats[] = {"zero", "one", "two", "few", "many", "other"};
    for (const char *c : cats)
        if (key == c) return true;
    return false;
}

/** Default ostream formatting, so script floats interpolate like JSON numbers. */
std::string numberToString(double v) {
    std::ostringstream os;
    os << v;
    return os.str();
}

void flatten(eve::json::Value node, const std::string &prefix,
             std::unordered_map<std::string, std::string> &strings,
             std::unordered_map<std::string, std::unordered_map<std::string, std::string>> &plurals) {
    if (node.isObject()) {
        const std::vector<std::string> names = node.keys();
        // A plain object of plural categories is a plural form table.
        bool allCats = !names.empty();
        bool allStrings = true;
        for (const auto &k : names) {
            if (!isPluralCategory(k)) {
                allCats = false;
                break;
            }
            if (!node.get(k.c_str()).isString()) allStrings = false;
        }
        if (allCats && allStrings && !prefix.empty()) {
            std::unordered_map<std::string, std::string> forms;
            for (const auto &k : names) forms[k] = node.getString(k.c_str());
            plurals[prefix] = std::move(forms);
            return;
        }
        for (const auto &k : names) {
            const std::string path = prefix.empty() ? k : prefix + "." + k;
            flatten(node.get(k.c_str()), path, strings, plurals);
        }
        return;
    }
    if (prefix.empty()) return;
    // Scalars stringify; arrays and nulls are not translatable and are skipped.
    if (node.isString() || node.isNumber() || node.isBool()) strings[prefix] = node.asString();
}

bool parseLocale(const std::string &text,
                 std::unordered_map<std::string, std::string> &strings,
                 std::unordered_map<std::string, std::unordered_map<std::string, std::string>> &plurals,
                 std::string *error) {
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

int64_t fileModtime(const std::string &path) {
    auto *fs = eve::ModuleManager::getInstance<eve::filesystem::Filesystem>("Filesystem");
    if (!fs) fs = eve::filesystem::Filesystem::create();
    eve::filesystem::Filesystem::Info info{};
    if (!fs->getInfo(path, info)) return -1;
    return info.modtime;
}

// Convert a Squirrel table (or null) into a string params map.
std::unordered_map<std::string, std::string> readParams(ssq::Object params) {
    std::unordered_map<std::string, std::string> out;
    HSQUIRRELVM vm = params.getHandle();
    if (!vm) return out;
    const HSQOBJECT raw = params.getRaw();
    if (raw._type != OT_TABLE) return out;

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, raw);
    sq_pushnull(vm);  // iterator
    while (SQ_SUCCEEDED(sq_next(vm, -2))) {
        // key at -2, value at -1
        const SQChar *k = nullptr;
        if (sq_gettype(vm, -2) == OT_STRING && SQ_SUCCEEDED(sq_getstring(vm, -2, &k)) && k) {
            std::string v;
            const SQObjectType vt = sq_gettype(vm, -1);
            if (vt == OT_STRING) {
                const SQChar *sv = nullptr;
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

I18n::Locale *I18n::findLocale(const std::string &lang) {
    auto it = locales_.find(lang);
    return it == locales_.end() ? nullptr : &it->second;
}

const I18n::Locale *I18n::findLocale(const std::string &lang) const {
    auto it = locales_.find(lang);
    return it == locales_.end() ? nullptr : &it->second;
}

bool I18n::loadFromJson(const std::string &lang, const std::string &json) {
    if (lang.empty()) return false;
    Locale loc;
    std::string error;
    if (!parseLocale(json, loc.strings, loc.plurals, &error)) return false;
    loc.path = "";
    loc.modtime = -1;
    locales_[lang] = std::move(loc);
    return true;
}

bool I18n::loadFromFile(const std::string &lang, const std::string &path) {
    if (lang.empty() || path.empty()) return false;

    auto *fs = ModuleManager::getInstance<filesystem::Filesystem>("Filesystem");
    if (!fs) fs = filesystem::Filesystem::create();

    filesystem::FileData *fd = nullptr;
    try {
        fd = fs->read(path);
    } catch (...) {
        delete fd;
        return false;
    }
    if (fd == nullptr || fd->getData() == nullptr || fd->getSize() == 0) {
        delete fd;
        return false;
    }

    std::string text(static_cast<const char *>(fd->getData()), fd->getSize());
    delete fd;

    Locale loc;
    std::string error;
    if (!parseLocale(text, loc.strings, loc.plurals, &error)) return false;

    loc.path = path;
    loc.modtime = fileModtime(path);
    fs->watch(path);
    locales_[lang] = std::move(loc);
    return true;
}

void I18n::unload(const std::string &lang) {
    locales_.erase(lang);
}

void I18n::clear() {
    locales_.clear();
}

bool I18n::setLanguage(const std::string &lang) {
    if (!hasLanguage(lang)) return false;
    language_ = lang;
    return true;
}

std::string I18n::getLanguageAt(int index) const {
    if (index < 0 || size_t(index) >= locales_.size()) return {};
    std::vector<std::string> langs;
    langs.reserve(locales_.size());
    for (const auto &[lang, loc] : locales_) langs.push_back(lang);
    std::sort(langs.begin(), langs.end());
    return langs[size_t(index)];
}

bool I18n::hasLanguage(const std::string &lang) const {
    return findLocale(lang) != nullptr;
}

bool I18n::has(const std::string &key) const {
    const Locale *cur = findLocale(language_);
    if (cur && (cur->strings.find(key) != cur->strings.end() || cur->plurals.find(key) != cur->plurals.end()))
        return true;
    if (defaultLanguage_ != language_) {
        if (const Locale *def = findLocale(defaultLanguage_))
            if (def->strings.find(key) != def->strings.end() || def->plurals.find(key) != def->plurals.end())
                return true;
    }
    return false;
}

std::string I18n::get(const std::string &key) const {
    if (const Locale *cur = findLocale(language_)) {
        auto it = cur->strings.find(key);
        if (it != cur->strings.end()) return it->second;
    }
    if (defaultLanguage_ != language_) {
        if (const Locale *def = findLocale(defaultLanguage_)) {
            auto it = def->strings.find(key);
            if (it != def->strings.end()) return it->second;
        }
    }
    return key;
}

std::string I18n::formatString(const std::string &tpl,
                               const std::unordered_map<std::string, std::string> &params) const {
    std::string out;
    out.reserve(tpl.size());
    for (size_t i = 0; i < tpl.size();) {
        if (tpl[i] == '{') {
            const size_t close = tpl.find('}', i + 1);
            if (close != std::string::npos) {
                const std::string name = tpl.substr(i + 1, close - i - 1);
                const auto        it   = params.find(name);
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

std::string I18n::getWithParams(const std::string &key,
                                const std::unordered_map<std::string, std::string> &params) const {
    return formatString(get(key), params);
}

std::string I18n::getPlural(const std::string &key, int n) const {
    return getPluralWithParams(key, n, {});
}

std::string I18n::getPluralWithParams(const std::string &key, int n,
                                      const std::unordered_map<std::string, std::string> &params) const {
    std::unordered_map<std::string, std::string> merged = params;
    merged["n"] = std::to_string(n);

    const Locale *cur = findLocale(language_);
    const Locale *def = (defaultLanguage_ != language_) ? findLocale(defaultLanguage_) : nullptr;
    if (!cur && !def) return key;

    // Look up in the current language first, then fall back to the default.
    std::string current;
    if (const Locale *loc = cur) {
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
    if (const Locale *loc = def) {
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

std::string I18n::pluralForm(const std::string &lang, int n) const {
    std::string base = lang;
    if (const size_t dash = base.find('-'); dash != std::string::npos) base = base.substr(0, dash);
    if (const size_t under = base.find('_'); under != std::string::npos) base = base.substr(0, under);

    if (base == "zh" || base == "ja" || base == "ko" || base == "th" || base == "vi" ||
        base == "id" || base == "ms" || base == "tr" || base == "my")
        return "other";

    if (base == "fr" || base == "pt") return (n == 0 || n == 1) ? "one" : "other";

    if (base == "ru" || base == "uk" || base == "be") {
        const int mod10 = n % 10;
        const int mod100 = n % 100;
        if (mod10 == 1 && mod100 != 11) return "one";
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14)) return "few";
        return "many";
    }

    if (base == "pl") {
        const int mod10 = n % 10;
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
    for (const auto &[lang, loc] : locales_) {
        if (loc.path.empty()) continue;
        const int64_t mt = fileModtime(loc.path);
        if (mt >= 0 && mt != loc.modtime) dirty.push_back(lang);
    }

    int reloaded = 0;
    for (const std::string &lang : dirty) {
        if (loadFromFile(lang, locales_[lang].path)) ++reloaded;
    }
    return reloaded;
}

// ---------------------------------------------------------------------------
// Script binding
// ---------------------------------------------------------------------------

void I18n::expose(ssq::Table &table) {
    auto cls = table.addClass(name, I18n::create, false);
    expose(cls);
}

void I18n::expose(ssq::Class &cls) {
    cls.addFunc("getName", &I18n::getName);
    cls.addFunc("loadFromJson", &I18n::loadFromJson);
    cls.addFunc("loadFromFile", &I18n::loadFromFile);
    cls.addFunc("unload", &I18n::unload);
    cls.addFunc("clear", &I18n::clear);
    cls.addFunc("setLanguage", &I18n::setLanguage);
    cls.addFunc("getLanguage", &I18n::getLanguage);
    cls.addFunc("setDefaultLanguage", &I18n::setDefaultLanguage);
    cls.addFunc("getDefaultLanguage", &I18n::getDefaultLanguage);
    cls.addFunc("getLanguageCount", &I18n::getLanguageCount);
    cls.addFunc("getLanguageAt", &I18n::getLanguageAt);
    cls.addFunc("hasLanguage", &I18n::hasLanguage);
    cls.addFunc("has", &I18n::has);
    cls.addFunc("get", &I18n::get);
    cls.addFunc("getWithParams", [](I18n *self, const std::string &key,
                                    ssq::Object params) -> std::string {
        return self->getWithParams(key, readParams(params));
    });
    cls.addFunc("getPlural", &I18n::getPlural);
    cls.addFunc("getPluralWithParams", [](I18n *self, const std::string &key, int n,
                                          ssq::Object params) -> std::string {
        return self->getPluralWithParams(key, n, readParams(params));
    });
    cls.addFunc("setAutoReload", &I18n::setAutoReload);
    cls.addFunc("isAutoReload", &I18n::isAutoReload);
    cls.addFunc("update", &I18n::update);
}

}  // namespace eve::i18n