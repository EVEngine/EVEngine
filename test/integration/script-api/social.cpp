#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "ScriptTest.h"
#include "social/Social.h"
#include "social/SocialGraph.h"

using eve::social::Social;
using eve::social::SocialGraph;


static const char* kSocialScript = R"SQ(
[[maybe_unused]] function testSocialBindings() {
    local social = eve.Social();
    social.clear();
    if (!social.setOwner("asset:b", "org")) return false;
    if (!social.setOwner("asset:a", "org")) return false;
    if (social.ownedAt("org", 0) != "asset:a") return false;
    if (!social.setControllerInt(7, 9)) return false;
    if (social.controllerOfInt(7) != social.idFromInt(9)) return false;
    if (!social.assign("person", "operator", "asset:a")) return false;
    if (social.assigneeAt("asset:a", "operator", 0) != "person") return false;
    if (!social.setRelation("person", "org", "affinity", 30.0)) return false;
    if (social.addRelation("person", "org", "affinity", 15.0) != 45.0) return false;
    if (social.relationSourceAt("org", "affinity", 40.0, 0) != "person") return false;
    if (social.eventCount() < 5) return false;
    return social.eventAction(social.eventCount() - 1) == "relation_changed";
}
)SQ";

UnitSciptTest(SocialScriptTest, kSocialScript);

TEST_CASE_FIXTURE(SocialScriptTest, "social.script.bindings") {
    CHECK(vm.callFunc(vm.findFunc("testSocialBindings"), vm).toBool());
}
