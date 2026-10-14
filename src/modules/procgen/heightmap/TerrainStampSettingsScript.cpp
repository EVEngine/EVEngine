#include "common/SquirrelBinding.h"
#include "procgen/MeshBuild.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainMeshStamp.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"

namespace eve::procgen {
void exposeTerrainStampSettings(ssq::Table& table) {
    auto settings = table.addClass("TerrainStampSettings", ssq::Class::Ctor<TerrainStampSettings()>());
    // The pinned VM uses float SQFloat; bind explicit float adapters for double world coordinates.
    settings.addFunc("setGrid", [](TerrainStampSettings* s, float x, float z, float dx, float dz) {
        s->originX  = x;
        s->originZ  = z;
        s->spacingX = dx;
        s->spacingZ = dz;
    });
    settings.addFunc("setCenter", [](TerrainStampSettings* s, float x, float z) {
        s->centerX = x;
        s->centerZ = z;
    });
    settings.addFunc("setSize", [](TerrainStampSettings* s, float width, float depth) {
        s->width = width;
        s->depth = depth;
    });
    settings.addFunc("setRotation", [](TerrainStampSettings* s, float radians) { s->rotation = radians; });
    settings.addVar("amplitude", &TerrainStampSettings::amplitude);
    settings.addVar("baseHeight", &TerrainStampSettings::baseHeight);
    settings.addVar("blendStrength", &TerrainStampSettings::blendStrength);

    settings.addVar("smoothWidth", &TerrainStampSettings::smoothWidth);
    settings.addVar("edgeFade", &TerrainStampSettings::edgeFade);
    auto builder = table.addClass("TerrainMeshStampBuilder", ssq::Class::Ctor<TerrainMeshStampBuilder()>());
    builder.addFunc("setSource", [vm = table.getHandle()](TerrainMeshStampBuilder* self, const MeshBuild* mesh) {
        auto result = mesh ? self->setSource(*mesh)
                           : Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                    "terrain.meshStamp: mesh required"));
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    builder.addFunc(
        "bake", [vm = table.getHandle()](const TerrainMeshStampBuilder* self, Heightmap* heights, Heightmap* coverage,
                                         float minX, float minZ, float width, float depth, float feather) {
            auto result = heights && coverage
                              ? self->bake(*heights, *coverage, minX, minZ, width, depth, feather)
                              : Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                       "terrain.meshStamp: outputs required"));
            return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
        });
}
}  // namespace eve::procgen
