#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "common/Capability.h"
#include "common/StateAccess.h"
#include "tags/TagStore.h"
#include "tags/Tags.h"

using eve::tags::Tags;
using eve::tags::TagStore;


static const char* kTagsScript = R"SQ(
[[maybe_unused]] function testTagsBindings() {
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
