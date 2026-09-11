#include "procgen/mesh/GeometryStroke.h"

#include <zeroerr/unittest.h>

#include <cmath>

using namespace eve::procgen;

namespace {

template <typename T>
void requireOk(const eve::Result<T>& result) {
    REQUIRE(result.ok());
}

}  // namespace

TEST_CASE("procgen.geometryStroke.buildsAllOfficialCrossSections") {
    struct Expected {
        const char* shape;
        int         totalIndices;
    };
    for (const Expected expected : {Expected{"quad", 24}, Expected{"triangularPrism", 42}, Expected{"cube", 60}}) {
        GeometryStroke stroke;
        requireOk(stroke.setShapeResult(expected.shape));
        requireOk(stroke.setSizeResult(0.5f, 0.3f));
        requireOk(stroke.addPointResult(0.f, 0.f, 0.f));
        requireOk(stroke.addPointResult(1.f, 0.f, 0.f));
        requireOk(stroke.addPointResult(1.f, 1.f, 0.f));
        auto mesh = stroke.buildMeshResult();
        requireOk(mesh);
        CHECK_EQ(mesh.value().getIndexCount(), expected.totalIndices);
        CHECK_EQ(mesh.value().getMeta("shape", ""), expected.shape);
        CHECK_EQ(mesh.value().getMeta("pointCount", ""), "3");
        CHECK_EQ(mesh.value().getGroupCount(), 1);
    }
}

TEST_CASE("procgen.geometryStroke.supportsPlanarSpacingUndoAndAtomicValidation") {
    GeometryStroke stroke;
    requireOk(stroke.setInputSpaceResult("planar", 2.f));
    requireOk(stroke.setMinimumSpacingResult(0.5f));
    auto first = stroke.addPointResult(0.f, 99.f, 0.f);
    auto close = stroke.addPointResult(0.1f, -5.f, 0.f);
    auto next  = stroke.addPointResult(1.f, -5.f, 0.f);
    requireOk(first);
    requireOk(close);
    requireOk(next);
    CHECK(first.value());
    CHECK(!close.value());
    CHECK(next.value());
    CHECK_EQ(stroke.pointCount(), 2);

    const auto revision = stroke.revision();
    CHECK(!stroke.setShapeResult("sphere").ok());
    CHECK_EQ(stroke.revision(), revision);
    auto mesh = stroke.buildMeshResult();
    requireOk(mesh);
    for (int index = 0; index < mesh.value().getVertexCount(); ++index)
        CHECK(std::abs(mesh.value().getPositionY(index) - 2.f) < 1e-6f);

    requireOk(stroke.undoResult());
    CHECK_EQ(stroke.pointCount(), 1);
    CHECK(!stroke.buildMeshResult().ok());
    requireOk(stroke.undoResult());
    CHECK(!stroke.undoResult().ok());
}
