#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "voxel_editing/VoxelCatalog.h"

using eve::voxel_editing::DomainOperation;
using eve::voxel_editing::EditorResult;
using eve::voxel_editing::ObjectId;
using eve::voxel_editing::VoxelCatalogTarget;
using eve::voxel_editing::VoxelCellFill;
using eve::voxel_editing::VoxelModelValue;
using eve::voxel_editing::VoxelSocketKind;

namespace {
void apply(VoxelCatalogTarget& target, EditorResult<DomainOperation> operation) {
    REQUIRE(operation.ok());
    REQUIRE(target.applyDomainOperation(operation.value()).ok());
}

VoxelModelValue cube(const std::string& id, int n) {
    VoxelModelValue model;
    model.id    = ObjectId(id);
    model.name  = id;
    model.sizeX = n;
    model.sizeY = n;
    model.sizeZ = n;
    for (int z = 0; z < n; ++z)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x) model.voxels.push_back({x, y, z});
    return model;
}
}  // namespace

TEST_CASE("editor.voxel.catalog_classifies_filled_and_partial_occupancy") {
    VoxelCatalogTarget target("project");
    apply(target, target.makeCreateModel(cube("block", 4)));
    CHECK_EQ(eve::voxel_editing::voxelClassifyModelFill(*target.findModel(ObjectId("block"))), VoxelCellFill::Filled);

    VoxelModelValue bed;
    bed.id    = ObjectId("bed");
    bed.name  = "bed";
    bed.sizeX = 8;
    bed.sizeY = 4;
    bed.sizeZ = 6;
    for (int x = 0; x < 8; ++x)
        for (int z = 0; z < 6; ++z) bed.voxels.push_back({x, 0, z});
    apply(target, target.makeCreateModel(bed));
    CHECK_EQ(eve::voxel_editing::voxelClassifyModelFill(*target.findModel(ObjectId("bed"))), VoxelCellFill::Partial);

    const auto pick = eve::voxel_editing::pickVoxelModel(*target.findModel(ObjectId("block")), -2.f, 1.5f, 1.5f, 1.f,
                                                         0.f, 0.f, 16.f);
    CHECK(pick.hit);
    CHECK(pick.canAttach);
    CHECK_EQ(pick.hitX, 0);
    CHECK_EQ(pick.prevX, -1);
}

TEST_CASE("editor.voxel.catalog_snapshot_roundtrip_preserves_occupancy") {
    VoxelCatalogTarget target("project");
    apply(target, target.makeCreateModel(cube("block", 2)));
    VoxelCatalogTarget restored("copy");
    REQUIRE(restored.loadSnapshot(target.snapshotValue()).ok());
    REQUIRE(restored.findModel(ObjectId("block")));
    CHECK_EQ(restored.findModel(ObjectId("block"))->voxels.size(), static_cast<std::size_t>(8));
}

TEST_CASE("editor.voxel.catalog_join_sockets_match_opposite_faces") {
    CHECK(eve::voxel_editing::canJoinVoxelSockets({"bed-rail", VoxelSocketKind::Male},
                                                  {"bed-rail", VoxelSocketKind::Female}));
    VoxelCatalogTarget target("project");
    VoxelModelValue head = cube("head", 2);
    head.sockets[0]      = {"bed-rail", VoxelSocketKind::Male};
    VoxelModelValue foot = cube("foot", 2);
    foot.sockets[1]      = {"bed-rail", VoxelSocketKind::Female};
    apply(target, target.makeCreateModel(head));
    apply(target, target.makeCreateModel(foot));
    const auto partners = target.hullJoinPartners(ObjectId("head"), 0);
    REQUIRE_EQ(partners.size(), static_cast<std::size_t>(1));
    CHECK_EQ(partners.front().value(), std::string("foot"));
}

TEST_CASE("editor.voxel.catalog_empty_canvas_attaches_first_in_bounds_cell") {
    VoxelModelValue empty;
    empty.id    = ObjectId("empty");
    empty.name  = "empty";
    empty.sizeX = 4;
    empty.sizeY = 4;
    empty.sizeZ = 4;
    const auto pick = eve::voxel_editing::pickVoxelModel(empty, -2.f, 1.5f, 1.5f, 1.f, 0.f, 0.f, 16.f);
    CHECK(!pick.hit);
    CHECK(pick.canAttach);
    CHECK_EQ(pick.prevX, 0);
    CHECK_EQ(pick.prevY, 1);
    CHECK_EQ(pick.prevZ, 1);
}
