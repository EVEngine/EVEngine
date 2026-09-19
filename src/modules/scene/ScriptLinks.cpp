// The scene link kind backed by user script.
//
// A node can already carry a renderable, a physics body, a camera or an audio
// source and have it follow the node's transform. This adds the kind that lets a
// *script* object do the same, so a user can attach their own object to the
// transform pipeline without a C++ build:
//
//     local follower = Follower()
//     eve.provider.publish("my.follower", follower)
//     scene.linkScriptAt("host", "node", "my.follower", "node")
//
// Two directions, matching the two link sync modes:
//
//   syncMode 0 ("node") -- for every dirty node, TransformSystem calls
//     `onNodeTransform(nodeId, worldX, worldY, worldZ, yaw, pitch, roll,
//      scaleX, scaleY, scaleZ)` on the published instance.
//   syncMode 1 ("body") -- before world propagation, the instance's
//     `nodeTransform()` returns `[x, y, z, yaw, pitch, roll]` in local space and
//     the node is moved to it.
//
// Dispatch stays a function-pointer table (scene/SceneLink.h), so the transform
// pipeline still never learns what the target is.
//
// Ownership: `SceneLink::target` points at a process-lifetime `ScriptLinkTarget`
// record, never at a script object. The record holds a generation-qualified
// provider handle, which gives three properties the pipeline needs:
//
//   * a retired generation is reported as a dead link instead of resolving to
//     whatever was published under that name next;
//   * the scene may still ask `alive()` about a record whose provider is gone,
//     so records are retained (moved to `retired`) rather than freed;
//   * no VM reference is held here at all -- `ScriptProviderRegistry` owns those,
//     and it drops them before the VM is destroyed.

#include "common/CrashLog.h"
#include "common/ScriptProvider.h"
#include "common/SquirrelBinding.h"
#include "scene/Scene.h"
#include "scene/SceneHost.h"
#include "scene/SceneInternal.h"
#include "scene/SceneLink.h"
#include "scene/TransformSystem.h"

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eve::scene {

namespace {

/** Registered kind name; also the key `unlinkNodeKindAt` takes. */
constexpr const char* kScriptKind = "script";

/** Method driven for syncMode 0. */
constexpr const char* kPushMethod = "onNodeTransform";

/** Method queried for syncMode 1; must return six numbers. */
constexpr const char* kPullMethod = "nodeTransform";

/** Elements `nodeTransform()` must return: x, y, z, yaw, pitch, roll. */
constexpr std::size_t kPullElementCount = 6;

/**
 * @brief One script object attached to a node, as the scene stores it.
 *
 * @ownership Holds a generation-qualified handle, never a script object.
 * @lifetime Process lifetime; see the file comment.
 */
struct ScriptLinkTarget {
    ScriptProviderHandle provider;
    /** A failure was already logged for this record; see the reporter below. */
    bool reported = false;
};

/**
 * @brief Process-lifetime pool of script link targets.
 *
 * `live` holds one record per currently published provider that has ever been
 * linked, so re-linking the same provider reuses its record instead of growing the
 * pool. Superseded or unloaded generations move to `retired`, where they stay
 * answerable for `alive()` until the scene has dropped their links.
 */
struct ScriptLinkTargets {
    std::vector<std::unique_ptr<ScriptLinkTarget>> live;
    std::vector<std::unique_ptr<ScriptLinkTarget>> retired;
};

ScriptLinkTargets& targets() {
    static ScriptLinkTargets state;
    return state;
}

/** Whether the provider generation this record names is still published. */
bool targetIsLive(const ScriptLinkTarget& target) {
    const ScriptProviderRegistry& registry = ScriptProviderRegistry::instance();
    return registry.generationOf(target.provider.name) == target.provider.generation;
}

/**
 * @brief Whether a failed provider call is the "generation already retired" case.
 *
 * `Result::code()` is the coarse `StatusCode`, which folds every stale handle into
 * `Rejected`; the diagnostic is what still names the specific reason.
 */
bool isStaleHandle(const Status& status) {
    const auto& diagnostics = status.diagnostics();
    return !diagnostics.empty() && diagnostics.front().code() == DiagnosticCode::StaleHandle;
}

/**
 * @brief Report one script link failure, at most once per record.
 *
 * Sync runs for every dirty node every frame, so a follower with a typo in its
 * method name would otherwise write a log line every frame. One line per linked
 * generation is enough to make the failure observable without flooding.
 */
void reportScriptLinkFailure(ScriptLinkTarget& link, const std::string& method, const Status& status) {
    if (link.reported) return;
    link.reported = true;
    recordLogEvent("error", "script link target '" + link.provider.name + "' method '" + method +
                                "' failed; the link was skipped this frame: " + status.describe());
}

/**
 * @brief Record for one provider, reusing an existing one when possible.
 *
 * @return A stable address to hand to `SceneLink::target`.
 * @remarks Never fails: a record is always obtainable for a published provider.
 */
ScriptLinkTarget* acquireTarget(const ScriptProviderHandle& provider) {
    ScriptLinkTargets& pool = targets();
    for (const auto& candidate : pool.live) {
        if (candidate->provider == provider) return candidate.get();
    }

    // Drop records whose generation is gone so `live` tracks published providers.
    for (auto it = pool.live.begin(); it != pool.live.end();) {
        if (targetIsLive(**it)) {
            ++it;
            continue;
        }
        pool.retired.push_back(std::move(*it));
        it = pool.live.erase(it);
    }

    pool.live.push_back(std::make_unique<ScriptLinkTarget>(ScriptLinkTarget{provider}));
    return pool.live.back().get();
}

/**
 * @brief Push one node's world transform into its script target.
 * @param target A record obtained from `acquireTarget`.
 * @cost One matrix decomposition plus one script call with ten arguments, per
 *       dirty node holding a "script" link, per frame. That is the "acceptable per
 *       frame, not per entity" tier the plan's non-goals describe.
 * @remarks A raising or missing method is reported through `eve.log` (once per
 *       generation) and otherwise ignored: a broken follower must not stall the
 *       transform pipeline for every other node.
 */
void pushNodeTransform(const SceneNode& node, void* target) {
    auto* link = static_cast<ScriptLinkTarget*>(target);
    if (!link) return;

    glm::vec3 position(0.f), euler(0.f), scale(1.f);  // euler = yaw, pitch, roll
    decomposeWorld(node.world, position, euler, scale);

    const ScriptArg args[] = {
        ScriptArg::fromText(node.id),      ScriptArg::fromNumber(position.x), ScriptArg::fromNumber(position.y),
        ScriptArg::fromNumber(position.z), ScriptArg::fromNumber(euler.x),    ScriptArg::fromNumber(euler.y),
        ScriptArg::fromNumber(euler.z),    ScriptArg::fromNumber(scale.x),    ScriptArg::fromNumber(scale.y),
        ScriptArg::fromNumber(scale.z),
    };

    const ScriptProviderRegistry& registry = ScriptProviderRegistry::instance();
    auto                          pushed   = registry.call(link->provider, kPushMethod, args);
    if (pushed) return;
    // A link whose provider was retired between the dirty marking and this call is
    // already on its way out; anything else is a real script error worth one line.
    if (isStaleHandle(pushed.status())) return;
    reportScriptLinkFailure(*link, kPushMethod, pushed.status());
}

/**
 * @brief Pull a script target's transform into the node it drives.
 * @remarks Fewer than six numbers is a rejected result, not a partial update, so
 *          the node is never left half-moved.
 */
void pullNodeTransform(SceneNode& node, void* target) {
    auto* link = static_cast<ScriptLinkTarget*>(target);
    if (!link) return;

    const ScriptProviderRegistry& registry = ScriptProviderRegistry::instance();
    auto                          values   = registry.callNumbers(link->provider, kPullMethod);
    if (!values) {
        if (isStaleHandle(values.status())) return;
        reportScriptLinkFailure(*link, kPullMethod, values.status());
        return;
    }
    if (values.value().size() < kPullElementCount) {
        reportScriptLinkFailure(*link, kPullMethod,
                                Status::failure(Diagnostic::error(
                                    DiagnosticCode::InvalidArgument,
                                    "script link method '" + std::string(kPullMethod) + "' returned " +
                                        std::to_string(values.value().size()) + " numbers; six are required",
                                    link->provider.name)));
        return;
    }

    node.x     = float(values.value()[0]);
    node.y     = float(values.value()[1]);
    node.z     = float(values.value()[2]);
    node.yaw   = float(values.value()[3]);
    node.pitch = float(values.value()[4]);
    node.roll  = float(values.value()[5]);
}

/** @brief Whether a script target is still worth dispatching. */
bool aliveScriptTarget(const void* target) {
    const auto* link = static_cast<const ScriptLinkTarget*>(target);
    return link != nullptr && targetIsLive(*link);
}

struct Register {
    Register() { registerLinkKind({kScriptKind, &pushNodeTransform, &pullNodeTransform, &aliveScriptTarget}); }
} g_register;

}  // namespace

void exposeScriptLinkBindings(ssq::Class& cls) {
    cls.addFunc(
        "linkScriptAt",
        [vm = cls.getHandle()](Scene* self, const std::string& hostName, const std::string& nodeId,
                               const std::string& providerName, const std::string& mode) -> ssq::Table {
            if (self == nullptr) {
                return script::projectResult(
                    vm, Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                               "linkScriptAt needs a live Scene instance", nodeId)));
            }
            return script::projectResult(vm, self->linkScriptAt(hostName, nodeId, providerName, mode));
        });
}

Result<void> Scene::linkScriptAt(const std::string& hostName, const std::string& nodeId,
                                const std::string& providerName, const std::string& mode) {
    SceneHost* host = resolveHost(hostName);
    if (!host) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "no scene host named '" + hostName + "'", providerName));
    }

    const ScriptProviderRegistry& registry   = ScriptProviderRegistry::instance();
    const std::uint64_t           generation = registry.generationOf(providerName);
    if (generation == 0) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "no script provider published as '" + providerName + "'", providerName));
    }

    const int kind = findLinkKind(kScriptKind);
    if (kind < 0) {
        // Cannot happen while this translation unit is linked, but a trimmed build
        // that dropped it must say so instead of linking nothing.
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Unsupported, "the 'script' link kind is not registered", providerName));
    }

    const ScriptProviderHandle provider{providerName, generation};
    if (!host->link(nodeId, kind, acquireTarget(provider), syncModeFromString(mode))) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "no node named '" + nodeId + "'", nodeId));
    }
    TransformSystem::updateHost(host);
    return Result<void>::success();
}

}  // namespace eve::scene
