#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "filesystem/Filesystem.h"
#include "i18n/I18n.h"

#include <cstring>
#include <string>

using namespace eve::i18n;


TEST_CASE("i18n.loadFromFileRoundTripAndMissing") {
    I18n* i18n = I18n::create();
    i18n->clear();
    CHECK(!i18n->loadFromFile("en", ""));
    CHECK(!i18n->loadFromFile("en", "nope.json"));
    CHECK(!i18n->loadFromFile("", "nope.json"));

    auto* fs = eve::filesystem::Filesystem::create();
    REQUIRE(fs->setIdentity("ev_ut_i18n", true));
    REQUIRE(fs->setupWriteDirectory());
    const char* json = R"({"hello": "world", "n": 42})";
    fs->write("i18n_locale.json", json, std::strlen(json));

    CHECK(i18n->loadFromFile("en", "i18n_locale.json"));
    CHECK(i18n->hasLanguage("en"));
    CHECK_EQ(i18n->get("hello"), std::string("world"));
    CHECK_EQ(i18n->get("n"), std::string("42"));
}
