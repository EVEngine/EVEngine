#include "procgen/editing/SplinePathBinder.h"

#include <zeroerr/unittest.h>

#include <map>
#include <string>
#include <utility>

using namespace eve::procgen_editing;

namespace {

SplinePathControlPoint binderPoint(const char* id, std::int64_t order, double x) {
    SplinePathControlPoint point;
    point.id    = StableId(id);
    point.order = order;
    point.x     = x;
    return point;
}

class FakeTransformSource final : public ISplineBindingTransformSource {
public:
    std::map<std::pair<std::string, std::string>, SplineBindingPose> poses;

    EditorResult<SplineBindingPose> resolve(const std::string& host, const std::string& object) const override {
        const auto found = poses.find({host, object});
        if (found == poses.end())
            return eve::editing::failed<SplineBindingPose>(EditorStatus::Conflict, RuleId("test.source-stale"),
                                                           "source is stale");
        return eve::editing::applied<SplineBindingPose>(found->second);
    }
};

}  // namespace

TEST_CASE("editor.splinePathBinder.refreshesAllBoundPointsAtomically") {
    SplinePathDocument document("path");
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(binderPoint("a", 0, 0.0)).value()).ok());
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(binderPoint("b", 1, 1.0)).value()).ok());
    FakeTransformSource source;
    source.poses[{"world", "left"}]  = {3.0, 4.0, 5.0};
    source.poses[{"world", "right"}] = {-2.0, 1.0, 7.0};
    SplinePathBinder binder("path");
    REQUIRE(binder.bindResult(document, StableId("a"), "world", "left", source).ok());
    REQUIRE(binder.bindResult(document, StableId("b"), "world", "right", source).ok());

    const auto revision = document.revision();
    REQUIRE(binder.refreshResult(document, source).ok());
    CHECK_EQ(document.revision(), revision + 2u);
    const auto points = document.points();
    CHECK_EQ(points[0].x, 3.0);
    CHECK_EQ(points[0].y, 4.0);
    CHECK_EQ(points[1].x, -2.0);
    CHECK_EQ(points[1].z, 7.0);
}

TEST_CASE("editor.splinePathBinder.staleSourcePreservesDocumentAndLinkForReload") {
    SplinePathDocument document("path");
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(binderPoint("a", 0, 2.0)).value()).ok());
    FakeTransformSource source;
    source.poses[{"world", "anchor"}] = {8.0, 0.0, 0.0};
    SplinePathBinder binder("path");
    REQUIRE(binder.bindResult(document, StableId("a"), "world", "anchor", source).ok());
    const auto snapshot = binder.snapshotValue();
    source.poses.clear();
    const auto revision = document.revision();
    auto       stale    = binder.refreshResult(document, source);
    REQUIRE(!stale.ok());
    CHECK_EQ(stale.code(), EditorStatus::Conflict);
    CHECK_EQ(document.revision(), revision);
    CHECK_EQ(document.points()[0].x, 2.0);
    CHECK_EQ(binder.bindings().size(), 1u);

    SplinePathBinder rebuilt("path");
    REQUIRE(rebuilt.loadSnapshot(snapshot).ok());
    source.poses[{"world", "anchor"}] = {9.0, 1.0, 0.0};
    REQUIRE(rebuilt.refreshResult(document, source).ok());
    CHECK_EQ(document.points()[0].x, 9.0);
    CHECK_EQ(document.points()[0].y, 1.0);
}

TEST_CASE("editor.splinePathBinder.rejectsDestroyedPointAndWrongDocument") {
    SplinePathDocument document("path");
    REQUIRE(document.applyDomainOperation(document.makeSetPoint(binderPoint("a", 0, 0.0)).value()).ok());
    FakeTransformSource source;
    source.poses[{"world", "anchor"}] = {1.0, 2.0, 3.0};
    SplinePathBinder binder("path");
    REQUIRE(binder.bindResult(document, StableId("a"), "world", "anchor", source).ok());
    REQUIRE(document.applyDomainOperation(document.makeDeletePoint(StableId("a")).value()).ok());
    const auto revision   = document.revision();
    auto       stalePoint = binder.refreshResult(document, source);
    REQUIRE(!stalePoint.ok());
    CHECK_EQ(document.revision(), revision);

    SplinePathDocument other("other");
    auto               wrong = binder.refreshResult(other, source);
    REQUIRE(!wrong.ok());
    CHECK_EQ(wrong.code(), EditorStatus::Rejected);
}

TEST_CASE("editor.splinePathBinder.providerAbsentIsObservable") {
    SceneQuerySplineBindingSource absent(nullptr);
    auto                          result = absent.resolve("world", "anchor");
    REQUIRE(!result.ok());
    CHECK_EQ(result.code(), EditorStatus::Unsupported);
}
