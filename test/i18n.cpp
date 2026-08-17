#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "i18n/I18n.h"

#include <string>

using namespace eve::i18n;

TEST_CASE("i18n.loadAndLookup") {
    I18n *i18n = I18n::create();
    REQUIRE(i18n != nullptr);
    i18n->clear();

    CHECK(i18n->loadFromJson("en", R"({
      "menu": { "start": "Start", "quit": "Quit" },
      "greeting": "Hello, {name}!",
      "items": { "one": "{n} item", "other": "{n} items" }
    })"));
    CHECK(i18n->loadFromJson("zh", R"({
      "menu": { "start": "\u5f00\u59cb", "quit": "\u9000\u51fa" },
      "greeting": "\u4f60\u597d\uff0c{name}\uff01",
      "items": { "one": "{n} \u4e2a", "other": "{n} \u4e2a" }
    })"));

    CHECK_EQ(i18n->getLanguageCount(), 2);
    CHECK(i18n->hasLanguage("en"));
    CHECK(i18n->hasLanguage("zh"));
    CHECK(!i18n->hasLanguage("fr"));

    CHECK(i18n->setLanguage("zh"));
    CHECK_EQ(i18n->getLanguage(), std::string("zh"));
    CHECK_EQ(i18n->get("menu.start"), std::string("\xe5\xbc\x80\xe5\xa7\x8b"));  // 开始
    CHECK_EQ(i18n->get("menu.quit"), std::string("\xe9\x80\x80\xe5\x87\xba"));    // 退出
    CHECK(i18n->has("menu.start"));
    CHECK(!i18n->has("missing.key"));

    // Unknown key returns the key itself.
    CHECK_EQ(i18n->get("menu.nope"), std::string("menu.nope"));
}

TEST_CASE("i18n.defaultLanguageFallback") {
    I18n *i18n = I18n::create();
    i18n->clear();
    i18n->loadFromJson("en", R"({"only_en": "English"})");
    i18n->loadFromJson("fr", R"({"menu.start": "D\u00e9marrer"})");

    i18n->setDefaultLanguage("en");
    i18n->setLanguage("fr");
    CHECK_EQ(i18n->get("menu.start"), std::string("D\xc3\xa9marrer"));
    // Key missing in current language falls back to default language.
    CHECK_EQ(i18n->get("only_en"), std::string("English"));

    // Switching to an unloaded language keeps the previous one.
    CHECK(!i18n->setLanguage("de"));
    CHECK_EQ(i18n->getLanguage(), std::string("fr"));
}

TEST_CASE("i18n.paramsInterpolation") {
    I18n *i18n = I18n::create();
    i18n->clear();
    i18n->loadFromJson("en", R"({
      "greeting": "Hello, {name}! You have {count} new messages.",
      "plain": "No placeholders here."
    })");

    std::unordered_map<std::string, std::string> params = {{"name", "World"}, {"count", "3"}};
    CHECK_EQ(i18n->getWithParams("greeting", params),
             std::string("Hello, World! You have 3 new messages."));
    CHECK_EQ(i18n->getWithParams("plain", params), std::string("No placeholders here."));

    // Missing params leave the placeholder untouched.
    CHECK_EQ(i18n->getWithParams("greeting", {}),
             std::string("Hello, {name}! You have {count} new messages."));
}

TEST_CASE("i18n.plurals") {
    I18n *i18n = I18n::create();
    i18n->clear();
    i18n->loadFromJson("en", R"({
      "items": { "one": "{n} item", "other": "{n} items" },
      "apples": { "one": "{n} apple", "other": "{n} apples" }
    })");

    CHECK_EQ(i18n->getPlural("items", 1), std::string("1 item"));
    CHECK_EQ(i18n->getPlural("items", 0), std::string("0 items"));
    CHECK_EQ(i18n->getPlural("items", 5), std::string("5 items"));
    CHECK_EQ(i18n->getPlural("apples", 1), std::string("1 apple"));
    CHECK_EQ(i18n->getPlural("apples", 21), std::string("21 apples"));

    // Plural with extra params.
    std::unordered_map<std::string, std::string> params = {{"kind", "red"}};
    CHECK_EQ(i18n->getPluralWithParams("items", 2, params), std::string("2 items"));
}

TEST_CASE("i18n.pluralRulesByLanguage") {
    I18n *i18n = I18n::create();
    i18n->clear();

    // Russian three-form plural rules.
    i18n->loadFromJson("ru", R"({
      "m": { "one": "{n} \u043e\u043a\u043d\u043e", "few": "{n} \u043e\u043a\u043d\u0430",
             "many": "{n} \u043e\u043a\u043e\u043d" }
    })");
    i18n->setLanguage("ru");
    CHECK_EQ(i18n->getPlural("m", 1), std::string("1 \xd0\xbe\xd0\xba\xd0\xbd\xd0\xbe"));
    CHECK_EQ(i18n->getPlural("m", 2), std::string("2 \xd0\xbe\xd0\xba\xd0\xbd\xd0\xb0"));
    CHECK_EQ(i18n->getPlural("m", 5), std::string("5 \xd0\xbe\xd0\xba\xd0\xbe\xd0\xbd"));
    CHECK_EQ(i18n->getPlural("m", 11), std::string("11 \xd0\xbe\xd0\xba\xd0\xbe\xd0\xbd"));
    CHECK_EQ(i18n->getPlural("m", 21), std::string("21 \xd0\xbe\xd0\xba\xd0\xbd\xd0\xbe"));
    CHECK_EQ(i18n->getPlural("m", 22), std::string("22 \xd0\xbe\xd0\xba\xd0\xbd\xd0\xb0"));

    // Chinese has no plural distinction -> "other".
    i18n->loadFromJson("zh", R"({"m": { "one": "{n}\u4e2a", "other": "{n}\u4e2a" }})");
    i18n->setLanguage("zh");
    CHECK_EQ(i18n->getPlural("m", 0), std::string("0\xe4\xb8\xaa"));
    CHECK_EQ(i18n->getPlural("m", 1), std::string("1\xe4\xb8\xaa"));
    CHECK_EQ(i18n->getPlural("m", 100), std::string("100\xe4\xb8\xaa"));

    // A locale that only defines "one"/"other" and defaults to "other" fallback.
    i18n->loadFromJson("en", R"({"m": { "one": "singular", "other": "plural" }})");
    i18n->setLanguage("en");
    CHECK_EQ(i18n->getPlural("m", 1), std::string("singular"));
    CHECK_EQ(i18n->getPlural("m", 2), std::string("plural"));
}

TEST_CASE("i18n.jsonNumbersAndEscapes") {
    I18n *i18n = I18n::create();
    i18n->clear();
    i18n->loadFromJson("en", R"({
      "pi": 3.14,
      "enabled": true,
      "line": "first\nsecond",
      "quote": "say \"hi\"",
      "unicode": "\u00e9"
    })");

    CHECK_EQ(i18n->get("pi"), std::string("3.14"));
    CHECK_EQ(i18n->get("enabled"), std::string("true"));
    CHECK_EQ(i18n->get("line"), std::string("first\nsecond"));
    CHECK_EQ(i18n->get("quote"), std::string("say \"hi\""));
    CHECK_EQ(i18n->get("unicode"), std::string("\xc3\xa9"));
}

TEST_CASE("i18n.invalidJson") {
    I18n *i18n = I18n::create();
    i18n->clear();
    CHECK(!i18n->loadFromJson("en", "{ not valid json"));
    CHECK(!i18n->loadFromJson("en", R"(["root", "must", "be", "object"])"));
    CHECK(!i18n->loadFromJson("", R"({"a": "b"})"));
    CHECK_EQ(i18n->getLanguageCount(), 0);
}

TEST_CASE("i18n.unloadAndClear") {
    I18n *i18n = I18n::create();
    i18n->clear();
    i18n->loadFromJson("en", R"({"a": "A"})");
    i18n->loadFromJson("de", R"({"a": "B"})");
    CHECK_EQ(i18n->getLanguageCount(), 2);

    i18n->unload("de");
    CHECK(!i18n->hasLanguage("de"));
    CHECK_EQ(i18n->getLanguageCount(), 1);

    i18n->clear();
    CHECK_EQ(i18n->getLanguageCount(), 0);
}

TEST_CASE("i18n.languageEnumeration") {
    I18n *i18n = I18n::create();
    i18n->clear();
    i18n->loadFromJson("zh", R"({"a": "1"})");
    i18n->loadFromJson("en", R"({"a": "2"})");
    i18n->loadFromJson("ja", R"({"a": "3"})");

    CHECK_EQ(i18n->getLanguageCount(), 3);
    CHECK_EQ(i18n->getLanguageAt(0), std::string("en"));
    CHECK_EQ(i18n->getLanguageAt(1), std::string("ja"));
    CHECK_EQ(i18n->getLanguageAt(2), std::string("zh"));
    CHECK_EQ(i18n->getLanguageAt(3), std::string(""));
}