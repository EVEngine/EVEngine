#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/StateValue.h"

#include <string>
#include <vector>

using namespace eve;

TEST_CASE("stateValue.factoriesAndKinds") {
    CHECK(StateValue::null().isNull());
    CHECK(StateValue::boolean(true).asBool());
    CHECK_EQ(StateValue::integer(42).asInt(), int64_t(42));
    CHECK(StateValue::number(1.5).asDouble() == 1.5);
    CHECK_EQ(StateValue::string("hi").asString(), std::string("hi"));
    CHECK(StateValue::array().isArray());
    CHECK(StateValue::object().isObject());
}

TEST_CASE("stateValue.objectSetFindKeys") {
    StateValue o = StateValue::object();
    o.set("a", StateValue::integer(1));
    o.set("b", StateValue::string("x"));

    const StateValue* a = o.find("a");
    REQUIRE(a != nullptr);
    CHECK_EQ(a->asInt(), int64_t(1));
    CHECK(o.find("missing") == nullptr);

    const std::vector<std::string> keys = o.keys();
    REQUIRE(keys.size() == 2u);
    CHECK_EQ(keys[0], std::string("a"));
    CHECK_EQ(keys[1], std::string("b"));

    // set() replaces in place without reordering or duplicating.
    o.set("a", StateValue::integer(2));
    CHECK_EQ(o.find("a")->asInt(), int64_t(2));
    CHECK_EQ(o.keys().size(), 2u);
}

TEST_CASE("stateValue.arrayOps") {
    StateValue a = StateValue::array();
    a.pushBack(StateValue::integer(10));
    a.pushBack(StateValue::string("v"));

    REQUIRE(a.arraySize() == 2u);
    CHECK_EQ(a.at(0).asInt(), int64_t(10));
    CHECK_EQ(a.at(1).asString(), std::string("v"));
}

TEST_CASE("stateValue.dottedPathGetSet") {
    StateValue o = StateValue::object();
    CHECK(o.setPath("player.level", StateValue::integer(3)));
    const StateValue* level = o.get("player.level");
    REQUIRE(level != nullptr);
    CHECK_EQ(level->asInt(), int64_t(3));

    StateValue items = StateValue::array();
    items.pushBack(StateValue::string("sword"));
    o.set("items", std::move(items));

    const StateValue* first = o.get("items[0]");
    REQUIRE(first != nullptr);
    CHECK_EQ(first->asString(), std::string("sword"));

    CHECK(o.get("player.missing") == nullptr);
    CHECK(o.get("items[5]") == nullptr);
    CHECK(o.get("player.level.x") == nullptr);  // scalar segment is not an object

    // Array element replacement on the final segment.
    CHECK(o.setPath("items[0]", StateValue::string("axe")));
    CHECK_EQ(o.get("items[0]")->asString(), std::string("axe"));

    // Non-object roots cannot take object paths; arrays must be pre-sized.
    StateValue n = StateValue::null();
    CHECK(!n.setPath("a.b", StateValue::integer(1)));
    CHECK(!o.setPath("items[2]", StateValue::integer(1)));
}

TEST_CASE("stateValue.mergeDefaults") {
    StateValue o = StateValue::object();
    o.set("keep", StateValue::integer(1));

    StateValue defaults = StateValue::object();
    defaults.set("keep", StateValue::integer(99));
    defaults.set("new", StateValue::string("d"));
    StateValue nested = StateValue::object();
    nested.set("x", StateValue::boolean(true));
    defaults.set("nested", nested);

    CHECK(o.mergeDefaults(defaults));
    CHECK_EQ(o.find("keep")->asInt(), int64_t(1));  // existing values untouched
    CHECK_EQ(o.find("new")->asString(), std::string("d"));
    const StateValue* n = o.find("nested");
    REQUIRE(n != nullptr);
    REQUIRE(n->isObject());
    CHECK(n->find("x")->asBool());

    // Merging into a non-object is a no-op; a second merge adds nothing.
    CHECK(!StateValue::integer(1).mergeDefaults(defaults));
    CHECK(!o.mergeDefaults(defaults));
}

TEST_CASE("stateValue.equality") {
    CHECK(StateValue::integer(1) == StateValue::integer(1));
    CHECK(StateValue::integer(1) != StateValue::integer(2));

    StateValue a = StateValue::object();
    a.set("k", StateValue::string("v"));
    StateValue b = StateValue::object();
    b.set("k", StateValue::string("v"));
    CHECK(a == b);
    b.set("k", StateValue::string("w"));
    CHECK(a != b);

    CHECK(StateValue::null() == StateValue::null());
}
