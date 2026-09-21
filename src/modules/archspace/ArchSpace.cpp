#include "archspace/ArchSpace.h"

#include "archspace/ArchSpaceDocument.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace eve::archspace {
namespace {

ssq::Table bakeArraysTable(HSQUIRRELVM vm, const MeshArrays& arrays) {
    ssq::Array positions(vm);
    for (float v : arrays.positions) positions.push(v);
    ssq::Array normals(vm);
    for (float v : arrays.normals) normals.push(v);
    ssq::Array uvs(vm);
    for (float v : arrays.uvs) uvs.push(v);
    ssq::Array indices(vm);
    for (std::uint32_t v : arrays.indices) indices.push(static_cast<std::int64_t>(v));
    ssq::Table table(vm);
    table.set("positions", positions);
    table.set("normals", normals);
    table.set("uvs", uvs);
    table.set("indices", indices);
    table.set("vertexCount", static_cast<std::int64_t>(arrays.vertexCount()));
    table.set("indexCount", static_cast<std::int64_t>(arrays.indexCount()));
    table.set("triangleCount", static_cast<std::int64_t>(arrays.triangleCount()));
    return table;
}

}  // namespace

Module_IMPL(ArchSpace, new ArchSpace());

void ArchSpace::expose(ssq::Table& table) {
    const HSQUIRRELVM vm = table.getHandle();
    table.addClass(name, ArchSpace::create, false);

    auto document =
        table.addClass<Document>("ArchSpaceDocument",
                                 std::function<Document*()>([] { return new Document(); }), true);
    document.addFunc("nodeCount", [](Document* self) {
        return self ? static_cast<int>(self->nodeCount()) : 0;
    });
    document.addFunc("rootId", [](Document* self) { return self ? self->rootId() : std::string{}; });
    document.addFunc("hasNode", [](Document* self, const std::string& id) {
        return self && self->find(id) != nullptr;
    });
    document.addFunc("bootstrap", [vm](Document* self, const std::string& siteId, const std::string& buildingId,
                                       const std::string& levelId, float levelHeight) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "ArchSpaceDocument receiver must not be null",
                                                                      "receiver", {}, "archspace.squirrel")));
        return eve::script::projectResult(vm, self->bootstrap(siteId, buildingId, levelId, levelHeight));
    });
    document.addFunc("createRectRoom", [vm](Document* self, const std::string& levelId, const std::string& roomId,
                                            const std::string& roomName, float originX, float originZ, float sizeX,
                                            float sizeZ, float wallHeight, float wallThickness, float slabThickness) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "ArchSpaceDocument receiver must not be null",
                                                                      "receiver", {}, "archspace.squirrel")));
        std::vector<Vec2> polygon = {{originX, originZ},
                                     {originX + sizeX, originZ},
                                     {originX + sizeX, originZ + sizeZ},
                                     {originX, originZ + sizeZ}};
        return eve::script::projectResult(
            vm, self->createRoom(levelId, roomId, roomName, std::move(polygon), wallHeight, wallThickness,
                                 slabThickness));
    });
    document.addFunc("createWall", [vm](Document* self, const std::string& levelId, const std::string& wallId,
                                        const std::string& wallName, float x0, float z0, float x1, float z1,
                                        float height, float thickness) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "ArchSpaceDocument receiver must not be null",
                                                                      "receiver", {}, "archspace.squirrel")));
        return eve::script::projectResult(
            vm, self->createWall(levelId, wallId, wallName, Vec2{x0, z0}, Vec2{x1, z1}, height, thickness));
    });
    document.addFunc("createOpening", [vm](Document* self, const std::string& wallId, const std::string& openingId,
                                           const std::string& openingName, const std::string& kindText, float t,
                                           float width, float height, float sill) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "ArchSpaceDocument receiver must not be null",
                                                                      "receiver", {}, "archspace.squirrel")));
        auto kind = parseOpeningKind(kindText);
        if (!kind.ok()) return eve::script::projectResult(vm, eve::Result<void>::failure(kind.status()));
        return eve::script::projectResult(
            vm, self->createOpening(wallId, openingId, openingName, kind.value(), t, width, height, sill));
    });
    document.addFunc("placeItem", [vm](Document* self, const std::string& levelId, const std::string& itemId,
                                       const std::string& itemName, const std::string& catalogId, float x, float y,
                                       float z, float yawDegrees) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "ArchSpaceDocument receiver must not be null",
                                                                      "receiver", {}, "archspace.squirrel")));
        return eve::script::projectResult(
            vm, self->placeItem(levelId, itemId, itemName, catalogId, Vec3{x, y, z}, yawDegrees));
    });
    document.addFunc("deleteNode", [vm](Document* self, const std::string& id) {
        if (!self)
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                      "ArchSpaceDocument receiver must not be null",
                                                                      "receiver", {}, "archspace.squirrel")));
        return eve::script::projectResult(vm, self->eraseCascade(id));
    });
    document.addFunc("bakeMeshArrays", [vm](Document* self) {
        if (!self) return ssq::Table(vm);
        return bakeArraysTable(vm, self->bakeMeshArrays());
    });
    document.addFunc("summary", [](Document* self) {
        if (!self) return std::string{"null"};
        const auto bake = self->bakeMeshArrays();
        return "nodes=" + std::to_string(self->nodeCount()) + " root=" + self->rootId() +
               " triangles=" + std::to_string(bake.triangleCount()) +
               " vertices=" + std::to_string(bake.vertexCount());
    });
}

void ArchSpace::expose(ssq::Class&) {}

}  // namespace eve::archspace
