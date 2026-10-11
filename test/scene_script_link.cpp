// Script-backed scene links (judgement criterion J-2).
//
// A user attaches their own object to the scene transform pipeline without a C++
// build: publish the instance through `eve.provider`, then link it with
// `scene.linkScriptAt`. These cases drive both sync directions through the real
// `TransformSystem` dispatch and pin the absent cases (no host, no node, nothing
// published, retired provider, broken target).

#include "ScriptTest.h"

#include "common/Diagnostic.h"
#include "common/ScriptProvider.h"
#include "common/Status.h"
#include "scene/NodeDesc.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "scene/TransformSystem.h"

#include <cmath>
#include <string>

using namespace eve;
using namespace eve::scene;

namespace {

const char* kScriptLinkContent = R"SQ(
class Follower {
    pushes = 0
    lastId = ""
    lastX = 0.0
    lastY = 0.0
    lastZ = 0.0
    lastYaw = 0.0
    lastScale = 0.0

    function onNodeTransform(nodeId, worldX, worldY, worldZ, yaw, pitch, roll, sx, sy, sz) {
        pushes += 1
        lastId = nodeId
        lastX = worldX
        lastY = worldY
        lastZ = worldZ
        lastYaw = yaw
        lastScale = sx
    }
}

// Drives the node it is linked to: the local transform the node must take.
class Driver {
    x = 0.0
    y = 0.0
    z = 0.0
    yaw = 0.0

    function nodeTransform() { return [x, y, z, yaw, 0.0, 0.0] }
}

// Deliberately incomplete: two numbers where the contract wants six.
class ShortDriver {
    function nodeTransform() { return [1.0, 2.0] }
}

// Deliberately broken: raising in the push direction must not stall its siblings.
class RaisingFollower {
    function onNodeTransform(nodeId, worldX, worldY, worldZ, yaw, pitch, roll, sx, sy, sz) {
        throw "follower is broken"
    }
}

follower <- Follower()
driver <- Driver()
shortDriver <- ShortDriver()
raisingFollower <- RaisingFollower()
)SQ";

bool approxEq(float actual, float expected, float tolerance = 1e-4f) {
    return std::fabs(actual - expected) <= tolerance;
}

eve::DiagnosticCode firstDiagnosticCode(const eve::Status& status) {
    return status.diagnostics().empty() ? eve::DiagnosticCode::None : status.diagnostics().front().code();
}

/** Reads one numeric member of a live script instance whatever numeric kind it is. */
double memberNumber(const ssq::Object& instance, const char* name) {
    HSQUIRRELVM     vm  = instance.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());
    sq_pushstring(vm, name, -1);
    double out = 0.0;
    if (SQ_SUCCEEDED(sq_get(vm, -2))) {
        if (sq_gettype(vm, -1) == OT_INTEGER) {
            SQInteger value = 0;
            if (SQ_SUCCEEDED(sq_getinteger(vm, -1, &value))) out = double(value);
        } else {
            SQFloat value = 0.f;
            if (SQ_SUCCEEDED(sq_getfloat(vm, -1, &value))) out = double(value);
        }
    }
    sq_settop(vm, top);
    return out;
}

/** Reads one string member of a live script instance; "" when it is missing. */
std::string memberString(const ssq::Object& instance, const char* name) {
    HSQUIRRELVM     vm  = instance.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());
    sq_pushstring(vm, name, -1);
    std::string out;
    if (SQ_SUCCEEDED(sq_get(vm, -2))) {
        const SQChar* value = nullptr;
        if (SQ_SUCCEEDED(sq_getstring(vm, -1, &value)) && value != nullptr) out = value;
    }
    sq_settop(vm, top);
    return out;
}

/** Writes one numeric member of a live script instance. */
bool setMemberNumber(const ssq::Object& instance, const char* name, float value) {
    HSQUIRRELVM     vm  = instance.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());
    sq_pushstring(vm, name, -1);
    sq_pushfloat(vm, SQFloat(value));
    const bool ok = SQ_SUCCEEDED(sq_newslot(vm, -3, SQFalse));
    sq_settop(vm, top);
    return ok;
}

/**
 * One script VM, one `eve.Scene` module, and the provider registry reset around it.
 * Each case mounts its own host name, so the cases stay independent in one process.
 */
class ScriptLinkTest : public ScriptTest {
public:
    ScriptLinkTest() : ScriptTest(kScriptLinkContent) {
        eve::ScriptProviderRegistry::instance().clearAll();
        module = Scene::create();
    }

    ~ScriptLinkTest() { eve::ScriptProviderRegistry::instance().clearAll(); }

protected:
    /** Mounts `root` under `name` and returns the host. */
    SceneHost* mount(const std::string& name, NodeDesc root) {
        auto mounted = module->mountAs(name, std::move(root));
        REQUIRE(mounted.ok());
        if (!mounted.ok()) return nullptr;
        return mounted.value();
    }

    /** Publishes a script instance under `name`; the instance must be in the VM. */
    eve::ScriptProviderHandle publish(const std::string& name, const char* instanceName) {
        auto published = eve::ScriptProviderRegistry::instance().publish(name, vm.find(instanceName));
        REQUIRE(published.ok());
        return published.value();
    }

    SceneNode* nodeOf(SceneHost* host, const std::string& id) {
        auto found = host->findById(id);
        REQUIRE(found.ok());
        return found.ok() ? found.value() : nullptr;
    }

    /** Moves a node locally and marks it dirty, the way a game system would. */
    void moveLocal(SceneHost* host, const std::string& id, float x, float y, float z) {
        SceneNode* node = nodeOf(host, id);
        REQUIRE(node != nullptr);
        if (node == nullptr) return;
        node->x          = x;
        node->y          = y;
        node->z          = z;
        node->localDirty = true;
        eve::scene::TransformSystem::updateHost(host);
    }

    /**
     * Marks every node dirty and runs one transform update.
     *
     * The pipeline only syncs dirty nodes, so a case that asserts "the follower was
     * called" has to say *when* the node moved instead of depending on the tree still
     * being dirty from mounting it.
     */
    void syncAll(SceneHost* host) {
        for (SceneNode& node : host->tree()->nodes) node.localDirty = true;
        eve::scene::TransformSystem::updateHost(host);
    }

    Scene* module = nullptr;
};

}  // namespace

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.pushDrivesFollower") {
    // Provider-present probe for the node -> script direction: the follower is
    // called with the node's *world* transform, not its local one.
    SceneHost* host = mount("scriptlink_push",
                            node("root", {node("child").withPosition(1.f, 2.f, 3.f)}).withPosition(10.f, 0.f, 0.f));

    REQUIRE(module->linkScriptAt("scriptlink_push", "child", publish("link.follower", "follower").name, "node").ok());
    CHECK_EQ(host->linkCount("child"), 1);

    syncAll(host);
    const ssq::Object follower = vm.find("follower");
    CHECK(memberNumber(follower, "pushes") >= 1.0);
    CHECK_EQ(memberString(follower, "lastId"), std::string("child"));
    CHECK_EQ(memberNumber(follower, "lastX"), 11.0);
    CHECK_EQ(memberNumber(follower, "lastY"), 2.0);
    CHECK_EQ(memberNumber(follower, "lastZ"), 3.0);
    CHECK_EQ(memberNumber(follower, "lastScale"), 1.0);

    const double pushes = memberNumber(follower, "pushes");

    // Moving the node re-syncs it; the reported position is still world-space.
    moveLocal(host, "child", 5.f, 6.f, 7.f);
    CHECK(memberNumber(follower, "pushes") > pushes);
    CHECK_EQ(memberNumber(follower, "lastX"), 15.0);
    CHECK_EQ(memberNumber(follower, "lastY"), 6.0);
    CHECK_EQ(memberNumber(follower, "lastZ"), 7.0);
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.pullDrivesNode") {
    // Script -> node direction: "body" mode asks the provider for a local transform
    // and moves the node to it, which is what physics does natively.
    SceneHost* host = mount("scriptlink_pull", node("root", {node("child").withPosition(1.f, 2.f, 3.f)}));

    const ssq::Object driver = vm.find("driver");
    REQUIRE(setMemberNumber(driver, "x", 4.f));
    REQUIRE(setMemberNumber(driver, "y", 5.f));
    REQUIRE(setMemberNumber(driver, "z", 6.f));
    REQUIRE(setMemberNumber(driver, "yaw", 0.25f));

    REQUIRE(module->linkScriptAt("scriptlink_pull", "child", publish("link.driver", "driver").name, "body").ok());

    // Linking already ran a transform update, so the node is at the driver's pose.
    const SceneNode* node = nodeOf(host, "child");
    REQUIRE(node != nullptr);
    CHECK(approxEq(node->x, 4.f));
    CHECK(approxEq(node->y, 5.f));
    CHECK(approxEq(node->z, 6.f));
    CHECK(approxEq(node->yaw, 0.25f));

    // And it keeps following: the node is not merely set once.
    REQUIRE(setMemberNumber(driver, "x", -1.5f));
    eve::scene::TransformSystem::updateHost(host);
    CHECK(approxEq(nodeOf(host, "child")->x, -1.5f));
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.absentInputsAreReported") {
    // J-4 absent probes: every way of not having a target is a structured failure,
    // and none of them silently creates a link.
    SceneHost* host = mount("scriptlink_absent", node("root", {node("child")}));

    auto unknownHost = module->linkScriptAt("scriptlink_nope", "child", "link.follower", "node");
    CHECK(!unknownHost.ok());
    CHECK_EQ(firstDiagnosticCode(unknownHost.status()), eve::DiagnosticCode::NotFound);

    auto unknownNode = module->linkScriptAt("scriptlink_absent", "nope", "link.follower", "node");
    CHECK(!unknownNode.ok());
    CHECK_EQ(firstDiagnosticCode(unknownNode.status()), eve::DiagnosticCode::NotFound);

    // Nothing published under that name: the socket is empty, and that is not a link.
    auto unpublished = module->linkScriptAt("scriptlink_absent", "child", "link.missing", "node");
    CHECK(!unpublished.ok());
    CHECK_EQ(firstDiagnosticCode(unpublished.status()), eve::DiagnosticCode::NotFound);

    CHECK_EQ(host->linkCount("child"), 0);

    // With the provider published but never linked, the pipeline has nothing to do.
    publish("link.follower", "follower");
    eve::scene::TransformSystem::updateHost(host);
    CHECK_EQ(memberNumber(vm.find("follower"), "pushes"), 0.0);
    CHECK_EQ(host->linkCount("child"), 0);
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.retiredProviderIsDropped") {
    // A retired generation must stop being driven *and* stop being linked: the
    // record stays answerable so the scene can purge it instead of following a
    // script object that no longer exists.
    SceneHost* host = mount("scriptlink_retire", node("root", {node("child").withPosition(1.f, 1.f, 1.f)}));

    const eve::ScriptProviderHandle handle = publish("link.follower", "follower");
    REQUIRE(module->linkScriptAt("scriptlink_retire", "child", handle.name, "node").ok());
    CHECK_EQ(host->linkCount("child"), 1);

    syncAll(host);
    const ssq::Object follower = vm.find("follower");
    const double      pushes   = memberNumber(follower, "pushes");
    REQUIRE(pushes >= 1.0);

    REQUIRE(eve::ScriptProviderRegistry::instance().unload(handle).ok());

    // The next sync drops the link instead of calling a retired generation.
    syncAll(host);
    CHECK_EQ(host->linkCount("child"), 0);
    CHECK_EQ(memberNumber(follower, "pushes"), pushes);
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.rejectedPullKeepsNodeIntact") {
    // A provider that returns too few numbers is rejected as a whole: the node must
    // not end up half-moved, and the link stays because the provider is alive.
    SceneHost* host = mount("scriptlink_short", node("root", {node("child").withPosition(1.f, 2.f, 3.f)}));

    REQUIRE(module->linkScriptAt("scriptlink_short", "child", publish("link.short", "shortDriver").name, "body").ok());

    const SceneNode* node = nodeOf(host, "child");
    REQUIRE(node != nullptr);
    CHECK(approxEq(node->x, 1.f));
    CHECK(approxEq(node->y, 2.f));
    CHECK(approxEq(node->z, 3.f));
    CHECK_EQ(host->linkCount("child"), 1);
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.brokenTargetDoesNotStallSiblings") {
    // A raising follower is reported once and skipped; the next node still syncs.
    // The broken one is linked first on purpose, so a stall would be visible.
    SceneHost* host =
        mount("scriptlink_broken",
              node("root", {node("bad").withPosition(1.f, 1.f, 1.f), node("good").withPosition(2.f, 2.f, 2.f)}));

    REQUIRE(
        module->linkScriptAt("scriptlink_broken", "bad", publish("link.raising", "raisingFollower").name, "node").ok());
    REQUIRE(module->linkScriptAt("scriptlink_broken", "good", publish("link.follower", "follower").name, "node").ok());

    const ssq::Object follower = vm.find("follower");
    const double      pushes   = memberNumber(follower, "pushes");

    syncAll(host);
    CHECK(memberNumber(follower, "pushes") > pushes);
    CHECK_EQ(memberNumber(follower, "lastX"), 2.0);
    CHECK_EQ(host->linkCount("bad"), 1);  // its target is alive, so the link survives
}

TEST_CASE_FIXTURE(ScriptLinkTest, "scene.scriptLink.scriptBindingReportsResult") {
    // The script face of the same call: a script links its own provider and reads the
    // result table, so `eve.Scene().linkScriptAt` is exercised, not just the C++ method.
    SceneHost* host = mount("scriptlink_api", node("root", {node("child").withPosition(3.f, 0.f, 0.f)}));
    publish("link.follower", "follower");

    vm.run(
        vm.compileSource("local scene = eve.Scene()\n"
                         "linked <- scene.linkScriptAt(\"scriptlink_api\", \"child\", \"link.follower\", \"node\")\n"
                         "missing <- scene.linkScriptAt(\"scriptlink_api\", \"child\", \"nope\", \"node\")\n"
                         "orphanHost <- scene.linkScriptAt(\"nowhere\", \"child\", \"link.follower\", \"node\")\n"));

    ssq::Object linked = vm.find("linked");
    CHECK_EQ(linked.getType(), ssq::Type::TABLE);
    CHECK(linked.find("ok").toBool());
    CHECK_EQ(host->linkCount("child"), 1);

    ssq::Object missing = vm.find("missing");
    CHECK_EQ(missing.getType(), ssq::Type::TABLE);
    CHECK(!missing.find("ok").toBool());
    CHECK_EQ(missing.find("code").toString(), std::string("not_found"));

    ssq::Object orphanHost = vm.find("orphanHost");
    CHECK(!orphanHost.find("ok").toBool());
    CHECK_EQ(orphanHost.find("code").toString(), std::string("not_found"));

    // The script-linked follower is the one the pipeline drives.
    syncAll(host);
    CHECK(memberNumber(vm.find("follower"), "lastX") >= 3.0);
}
