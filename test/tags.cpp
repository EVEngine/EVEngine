#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "tags/TagStore.h"
#include "tags/Tags.h"

using eve::tags::TagStore;

TEST_CASE("tags.tagsAreSetsWithDeterministicOrdering") {
    TagStore tags;
    CHECK(tags.addTag("subject:b", "zeta"));
    CHECK(tags.addTag("subject:b", "alpha"));
    CHECK(!tags.addTag("subject:b", "alpha"));
    CHECK(tags.hasTag("subject:b", "alpha"));

    const auto values = tags.tagsOf("subject:b");
    REQUIRE(values.size() == 2);
    CHECK(values[0] == "alpha");
    CHECK(values[1] == "zeta");
    CHECK(tags.removeTag("subject:b", "alpha"));
    CHECK(!tags.removeTag("subject:b", "alpha"));
    CHECK(!tags.hasTag("subject:b", "alpha"));
}

TEST_CASE("tags.reverseTagQueriesAreIndexedAndSorted") {
    TagStore tags;
    tags.addTag("subject:z", "selectable");
    tags.addTag("subject:a", "selectable");
    tags.addTag("subject:m", "other");

    const auto subjects = tags.subjectsWithTag("selectable");
    REQUIRE(subjects.size() == 2);
    CHECK(subjects[0] == "subject:a");
    CHECK(subjects[1] == "subject:z");
    tags.removeTag("subject:a", "selectable");
    REQUIRE(tags.subjectsWithTag("selectable").size() == 1);
    CHECK(tags.subjectsWithTag("selectable")[0] == "subject:z");
}

TEST_CASE("tags.requireAllAndAnyHaveExplicitEmptySetSemantics") {
    TagStore tags;
    tags.addTag("subject", "a");
    tags.addTag("subject", "b");
    tags.addCapability("subject", "x");
    tags.addCapability("subject", "y");

    CHECK(tags.requireAllTags("subject", {"a", "b"}));
    CHECK(!tags.requireAllTags("subject", {"a", "missing"}));
    CHECK(tags.requireAnyTag("subject", {"missing", "b"}));
    CHECK(!tags.requireAnyTag("subject", {}));
    CHECK(tags.requireAllTags("missing", {}));
    CHECK(tags.requireAllCapabilities("subject", {"x", "y"}));
    CHECK(tags.requireAnyCapability("subject", {"missing", "x"}));
    CHECK(!tags.requireAnyCapability("missing", {"x"}));
}

TEST_CASE("tags.capabilitiesAreIndependentFromTags") {
    TagStore tags;
    CHECK(tags.addTag("subject", "shared-name"));
    CHECK(tags.addCapability("subject", "shared-name"));
    CHECK(tags.removeTag("subject", "shared-name"));
    CHECK(!tags.hasTag("subject", "shared-name"));
    CHECK(tags.hasCapability("subject", "shared-name"));
    CHECK(tags.removeCapability("subject", "shared-name"));
    CHECK(!tags.hasCapability("subject", "shared-name"));
}

TEST_CASE("tags.rejectEmptyStableIdentifiersWithoutEvents") {
    TagStore tags;
    CHECK(!tags.addTag("", "tag"));
    CHECK(!tags.addTag("subject", ""));
    CHECK(!tags.addCapability("", "capability"));
    CHECK(!tags.addCapability("subject", ""));
    CHECK(tags.events().empty());
}

TEST_CASE("tags.eventsDescribeOnlySuccessfulMutations") {
    TagStore tags;
    tags.addTag("subject", "tag");
    tags.addTag("subject", "tag");
    tags.addCapability("subject", "capability");
    tags.removeTag("subject", "tag");
    REQUIRE(tags.events().size() == 3);
    CHECK_EQ(tags.events()[0].sequence, std::uint64_t(1));
    CHECK_EQ(tags.events()[1].sequence, std::uint64_t(2));
    CHECK(tags.events()[0].action == "tag_added");
    CHECK(tags.events()[0].subject == "subject");
    CHECK(tags.events()[0].value == "tag");
    CHECK(tags.events()[1].action == "capability_added");
    CHECK(tags.events()[2].action == "tag_removed");
    tags.clearEvents();
    CHECK(tags.events().empty());
    CHECK(tags.hasCapability("subject", "capability"));
}

TEST_CASE("tags.removeSubjectCleansBothSetsAndReverseIndex") {
    TagStore tags;
    tags.addTag("subject", "one");
    tags.addTag("subject", "two");
    tags.addCapability("subject", "ability");
    tags.clearEvents();

    CHECK(tags.removeSubject("subject"));
    CHECK(tags.tagsOf("subject").empty());
    CHECK(tags.capabilitiesOf("subject").empty());
    CHECK(tags.subjectsWithTag("one").empty());
    REQUIRE(tags.events().size() == 4);
    CHECK(tags.events().back().action == "subject_removed");
    CHECK(!tags.removeSubject("subject"));
}

TEST_CASE("tags.clearResetsStateEventsAndSequence") {
    TagStore tags;
    tags.addTag("subject", "tag");
    tags.clear();
    CHECK(tags.tagsOf("subject").empty());
    CHECK(tags.events().empty());
    tags.addCapability("new-subject", "new-capability");
    REQUIRE(tags.events().size() == 1);
    CHECK_EQ(tags.events()[0].sequence, std::uint64_t(1));
}

static const char* kTagsScript = R"SQ(
function testTagsBindings() {
    local tags = eve.Tags();
    if (!tags.add("subject:b", "zeta")) return false;
    if (!tags.add("subject:b", "alpha")) return false;
    if (!tags.add("subject:a", "alpha")) return false;
    if (tags.at("subject:b", 0) != "alpha") return false;
    if (tags.subjectAt("alpha", 0) != "subject:a") return false;
    if (!tags.requireAll("subject:b", ["alpha", "zeta"])) return false;
    if (!tags.requireAny("subject:b", ["missing", "zeta"])) return false;
    if (!tags.addCapability("subject:b", "can.perform")) return false;
    if (!tags.requireAllCapabilities("subject:b", ["can.perform"])) return false;
    if (!tags.hasCapability("subject:b", "can.perform")) return false;
    return tags.eventAction(tags.eventCount() - 1) == "capability_added";
}
)SQ";

UnitSciptTest(TagsScriptTest, kTagsScript);

TEST_CASE_FIXTURE(TagsScriptTest, "tags.script.bindings") {
    CHECK(vm.callFunc(vm.findFunc("testTagsBindings"), vm).toBool());
}
