#include "procgen/Params.h"
#include "procgen/algorithms/BushMesh.h"
#include "zeroerr/unittest.h"

#include <cmath>
#include <string>

using eve::procgen::MeshBuild;
using eve::procgen::Params;

namespace {

struct Vec3 {
    float x;
    float y;
    float z;
};

Vec3 position(const MeshBuild &mesh, int index) {
    return {mesh.getPositionX(index), mesh.getPositionY(index), mesh.getPositionZ(index)};
}

Vec3 normal(const MeshBuild &mesh, int index) {
    return {mesh.getNormalX(index), mesh.getNormalY(index), mesh.getNormalZ(index)};
}

Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

}  // namespace

TEST_CASE("procgen.mesh.bush.outwardFoliageWinding") {
    Params params;
    params.setSeed(20260815u);
    params.setString("style", "mound");
    params.setString("leafMode", "blobs");
    params.setInt("blobs", 3);
    params.setInt("rings", 5);
    params.setInt("radialSegments", 12);
    params.setInt("twigs", 0);

    MeshBuild mesh;
    std::string error;
    REQUIRE(eve::procgen::generateBushMesh(params, mesh, error));

    int checkedTriangles = 0;
    for (int i = 0; i < mesh.getIndexCount(); i += 3) {
        const int ia = mesh.getIndex(i);
        const int ib = mesh.getIndex(i + 1);
        const int ic = mesh.getIndex(i + 2);
        const Vec3 face = cross(subtract(position(mesh, ib), position(mesh, ia)),
                                subtract(position(mesh, ic), position(mesh, ia)));
        if (dot(face, face) < 1e-12f) continue;
        const Vec3 averagedNormal{
            normal(mesh, ia).x + normal(mesh, ib).x + normal(mesh, ic).x,
            normal(mesh, ia).y + normal(mesh, ib).y + normal(mesh, ic).y,
            normal(mesh, ia).z + normal(mesh, ib).z + normal(mesh, ic).z,
        };
        CHECK(dot(face, averagedNormal) > 0.f);
        ++checkedTriangles;
    }
    CHECK(checkedTriangles > 0);
}
