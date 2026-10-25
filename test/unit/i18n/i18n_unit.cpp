// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "i18n.cpp"
  ]
}
*/
// clang-format on

#include "i18n/I18n.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <string>

using eve::i18n::I18n;

namespace {
I18n* fresh() {
    auto* module = I18n::create();
    REQUIRE(module != nullptr);
    module->clear();
    module->setDefaultLanguage("en");
    module->setAutoReload(false);
    return module;
}
}  // namespace

TEST_CASE("I18n locale lookup and formatting contract") {
    SUB_CASE("i18n contract: locale replacement flattens keys and accepts JSON scalar values") {
        auto* i18n = fresh();
        REQUIRE(i18n->replaceLocaleFromJson("en", R"({
      "menu":{"start":"Start","quit":"Quit"},
      "pi":3.14,"enabled":true,"line":"first\nsecond","quote":"say \"hi\"","unicode":"\u00e9"
    })")
                    .ok());
        REQUIRE_EQ(i18n->getLanguageCount(), 1);
        REQUIRE_EQ(i18n->getKeyCount("en"), 7);
        REQUIRE_EQ(i18n->get("menu.start"), "Start");
        REQUIRE_EQ(i18n->get("menu.quit"), "Quit");
        REQUIRE_EQ(i18n->get("pi"), "3.14");
        REQUIRE_EQ(i18n->get("enabled"), "true");
        REQUIRE_EQ(i18n->get("line"), "first\nsecond");
        REQUIRE_EQ(i18n->get("quote"), "say \"hi\"");
        REQUIRE_EQ(i18n->get("unicode"), "\xc3\xa9");
        REQUIRE_EQ(i18n->get("missing.key"), "missing.key");
    };

    SUB_CASE("i18n contract: failed locale parse and selection preserve published state") {
        auto* i18n = fresh();
        REQUIRE(i18n->replaceLocaleFromJson("en", R"({"greeting":"Hello"})").ok());
        REQUIRE(i18n->selectLanguage("en").ok());
        REQUIRE(!i18n->replaceLocaleFromJson("en", "{ broken").ok());
        REQUIRE(!i18n->replaceLocaleFromJson("en", R"(["not","object"])").ok());
        REQUIRE(!i18n->replaceLocaleFromJson("", R"({"greeting":"wrong"})").ok());
        REQUIRE_EQ(i18n->get("greeting"), "Hello");
        REQUIRE(!i18n->selectLanguage("de").ok());
        REQUIRE_EQ(i18n->getLanguage(), "en");
        REQUIRE_EQ(i18n->getLanguageCount(), 1);
    };

    SUB_CASE("i18n contract: lookup uses selected locale then default then raw key") {
        auto* i18n = fresh();
        REQUIRE(i18n->replaceLocaleFromJson("en", R"({"shared":"English","only_en":"Fallback"})").ok());
        REQUIRE(i18n->replaceLocaleFromJson("fr", R"({"shared":"Français"})").ok());
        REQUIRE(i18n->selectLanguage("fr").ok());
        REQUIRE_EQ(i18n->get("shared"), "Français");
        REQUIRE_EQ(i18n->get("only_en"), "Fallback");
        REQUIRE(i18n->has("only_en"));
        REQUIRE(!i18n->hasInLanguage("fr", "only_en"));
        REQUIRE_EQ(i18n->get("unknown"), "unknown");
        REQUIRE(!i18n->has("unknown"));
    };

    SUB_CASE("i18n contract: placeholders replace known names and leave missing names intact") {
        auto* i18n = fresh();
        REQUIRE(
            i18n->replaceLocaleFromJson(
                    "en", R"({"greeting":"Hello, {name}! You have {count} new messages.","plain":"No placeholders."})")
                .ok());
        REQUIRE_EQ(i18n->getWithParams("greeting", {{"name", "Eve"}, {"count", "3"}}),
                   "Hello, Eve! You have 3 new messages.");
        REQUIRE_EQ(i18n->getWithParams("greeting", {{"name", "Eve"}}), "Hello, Eve! You have {count} new messages.");
        REQUIRE_EQ(i18n->getWithParams("plain", {{"unused", "x"}}), "No placeholders.");
    };

    SUB_CASE("i18n contract: language-specific plural forms choose one few many and other") {
        auto* i18n = fresh();
        REQUIRE(i18n->replaceLocaleFromJson("en", R"({"items":{"one":"{n} item","other":"{n} items"}})").ok());
        REQUIRE(
            i18n->replaceLocaleFromJson("ru", R"({"items":{"one":"{n} one","few":"{n} few","many":"{n} many"}})").ok());
        REQUIRE(i18n->replaceLocaleFromJson("zh-CN", R"({"items":{"other":"{n} 件"}})").ok());
        REQUIRE(i18n->selectLanguage("en").ok());
        REQUIRE_EQ(i18n->getPlural("items", 0), "0 items");
        REQUIRE_EQ(i18n->getPlural("items", 1), "1 item");
        REQUIRE_EQ(i18n->getPlural("items", 21), "21 items");
        REQUIRE(i18n->selectLanguage("ru").ok());
        REQUIRE_EQ(i18n->getPlural("items", 1), "1 one");
        REQUIRE_EQ(i18n->getPlural("items", 2), "2 few");
        REQUIRE_EQ(i18n->getPlural("items", 5), "5 many");
        REQUIRE_EQ(i18n->getPlural("items", 11), "11 many");
        REQUIRE_EQ(i18n->getPlural("items", 21), "21 one");
        REQUIRE_EQ(i18n->getPlural("items", 22), "22 few");
        REQUIRE(i18n->selectLanguage("zh-CN").ok());
        REQUIRE_EQ(i18n->getPlural("items", 0), "0 件");
        REQUIRE_EQ(i18n->getPlural("items", 1), "1 件");
        REQUIRE_EQ(i18n->getPlural("items", 100), "100 件");
    };
}

TEST_CASE("I18n strict bundle and lifecycle contract") {
    SUB_CASE("i18n contract: strict bundle admits complete locales with matching placeholders") {
        auto* i18n     = fresh();
        auto  admitted = i18n->replaceBundleFromJson(R"({
      "schema":"eve.i18n.bundle","version":1,"defaultLocale":"en",
      "locales":{
        "en":{"dialogue":{"welcome":"Welcome, {name}!"},"items":{"one":"{n} item","other":"{n} items"}},
        "zh-CN":{"dialogue":{"welcome":"欢迎，{name}！"},"items":{"other":"{n} 件物品"}}
      }
    })");
        REQUIRE(admitted.ok());
        REQUIRE_EQ(admitted.value(), 2);
        REQUIRE_EQ(i18n->getDefaultLanguage(), "en");
        REQUIRE_EQ(i18n->getKeyCount("en"), 2);
        REQUIRE(i18n->hasInLanguage("zh-CN", "dialogue.welcome"));
        auto coverage = i18n->validateKeyCoverage("dialogue.welcome");
        REQUIRE(coverage.ok());
        REQUIRE_EQ(coverage.value(), 2);
        REQUIRE(i18n->selectLanguage("zh-CN").ok());
        REQUIRE_EQ(i18n->getWithParams("dialogue.welcome", {{"name", "Eve"}}), "欢迎，Eve！");
    };

    SUB_CASE("i18n contract: malformed bundle leaves selected and default locales untouched") {
        auto* i18n = fresh();
        REQUIRE(i18n->replaceBundleFromJson(R"({
      "schema":"eve.i18n.bundle","version":1,"defaultLocale":"en",
      "locales":{"en":{"message":"Hello {name}"},"fr":{"message":"Bonjour {name}"}}
    })")
                    .ok());
        REQUIRE(i18n->selectLanguage("fr").ok());
        for (
            const std::string invalid :
            {R"({"schema":"eve.i18n.bundle","version":1,"defaultLocale":"en","locales":{"en":{"message":"Hello {name}"},"fr":{"message":"Bonjour"}}})",
             R"({"schema":"eve.i18n.bundle","version":2,"defaultLocale":"en","locales":{"en":{"message":"Hello"}}})",
             R"({"schema":"eve.i18n.bundle","version":1,"defaultLocale":"en","extra":true,"locales":{"en":{"message":"Hello"}}})",
             R"({"schema":"eve.i18n.bundle","version":1,"defaultLocale":"missing","locales":{"en":{"message":"Hello"}}})"}) {
            REQUIRE(!i18n->replaceBundleFromJson(invalid).ok());
            REQUIRE_EQ(i18n->getLanguage(), "fr");
            REQUIRE_EQ(i18n->getDefaultLanguage(), "en");
            REQUIRE_EQ(i18n->getWithParams("message", {{"name", "Eve"}}), "Bonjour Eve");
        }
    };

    SUB_CASE("i18n contract: exact key coverage rejects missing and invalid key paths") {
        auto* i18n = fresh();
        REQUIRE(i18n->replaceLocaleFromJson("en", R"({"menu":{"start":"Start"}})").ok());
        REQUIRE(i18n->replaceLocaleFromJson("fr", R"({"menu":{"quit":"Quit"}})").ok());
        REQUIRE(!i18n->validateKeyCoverage("menu.start").ok());
        REQUIRE(!i18n->validateKeyCoverage("").ok());
        REQUIRE(!i18n->validateKeyCoverage("menu..start").ok());
        REQUIRE(!i18n->validateKeyCoverage("menu.").ok());
    };

    SUB_CASE("i18n contract: unload enumeration clear and JSON-only reload behavior") {
        auto* i18n = fresh();
        REQUIRE(i18n->replaceLocaleFromJson("zh", R"({"a":"1"})").ok());
        REQUIRE(i18n->replaceLocaleFromJson("en", R"({"a":"2"})").ok());
        REQUIRE(i18n->replaceLocaleFromJson("ja", R"({"a":"3"})").ok());
        REQUIRE_EQ(i18n->getLanguageAt(0), "en");
        REQUIRE_EQ(i18n->getLanguageAt(1), "ja");
        REQUIRE_EQ(i18n->getLanguageAt(2), "zh");
        REQUIRE_EQ(i18n->getLanguageAt(3), "");
        const auto updated = i18n->update(0.f);
        REQUIRE_EQ(updated, 0);
        i18n->unload("ja");
        REQUIRE(!i18n->hasLanguage("ja"));
        REQUIRE_EQ(i18n->getLanguageCount(), 2);
        i18n->clear();
        REQUIRE_EQ(i18n->getLanguageCount(), 0);
    };
}
