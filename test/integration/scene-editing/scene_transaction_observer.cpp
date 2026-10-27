// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "scene_editing"
}
*/
// clang-format on

#include "scene/SceneHost.h"
#include "scene/editing/SceneTarget.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <stdexcept>

TEST_CASE("scene.transaction.observer_exception_does_not_reject_published_state") {
    using namespace eve::editing;
    auto created = eve::scene::SceneHost::createHost("transaction-observer");
    REQUIRE(created.ok());
    auto* host = created.value();
    host->setTree(eve::scene::node("root", {eve::scene::node("child")}));
    eve::scene_editing::SceneHostEditorTarget target("observer-target", host);
    auto                                      candidate = target.cloneDomainState();
    auto* staged = dynamic_cast<eve::scene_editing::SceneHostEditorTarget*>(candidate.get());
    REQUIRE(staged != nullptr);
    auto operation = staged->makeRename(ObjectId("child"), "Committed");
    REQUIRE(operation.ok());
    auto applied = candidate->applyDomainOperation(operation.value());
    REQUIRE(applied.ok());
    host->setEventHandler(
        [](auto*, const auto&, const auto&, const auto&) { throw std::runtime_error("injected observer failure"); });
    auto published = target.commitDomainState(std::move(candidate));
    CHECK(published.ok());
    CHECK_EQ(published.diagnostics().size(), 1u);
    CHECK_EQ(host->findById("child").value()->name, std::string("Committed"));
    host->setEventHandler({});
    ecs::DestroyEntity(host);
}
