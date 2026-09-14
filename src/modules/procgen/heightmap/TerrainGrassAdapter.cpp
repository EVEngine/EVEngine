#include "procgen/heightmap/TerrainGrassAdapter.h"
#include "common/SquirrelBinding.h"
#include "graphics/Grass.h"
#include "image/ImageData.h"
#include "procgen/PointSet.h"
namespace eve::procgen {
namespace {
Result<int> bake(graphics::GrassField& field, const PointSet& points, const std::string& asset, float width,
                 float height, const image::ImageData* image) {
    if (asset.empty())
        return Result<int>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "terrain.grass: asset selector required"));
    std::vector<graphics::grass::Point> roots;
    for (int i = 0; i < points.getCount(); ++i) {
        if (points.getStringAttribute(i, "asset", "") != asset) continue;
        const auto& point = points.points()[size_t(i)];
        if (point.id == 0 || point.scaleX != point.scaleZ || point.scaleX <= 0.001F)
            return Result<int>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "terrain.grass: stable ID and equal positive horizontal X/Z scales above 0.001 required"));
        graphics::grass::Point root;
        root.position   = {point.x, point.y, point.z};
        root.scale      = point.scaleY;
        root.widthScale = point.scaleX;
        root.id         = uint32_t((point.id ^ (point.id >> 32)) & 0xffffffu);
        root.tint       = {point.colorR, point.colorG, point.colorB};
        roots.push_back(root);
    }
    return image ? field.bakeTexturedPoints(roots, width, height, *image) : field.bakePoints(roots, width, height);
}
}  // namespace
Result<int> bakeTerrainGrass(graphics::GrassField& field, const PointSet& points, const std::string& asset, float width,
                             float height) {
    return bake(field, points, asset, width, height, nullptr);
}
Result<int> bakeTerrainGrassImage(graphics::GrassField& field, const PointSet& points, const std::string& asset,
                                  float width, float height, const image::ImageData& image) {
    return bake(field, points, asset, width, height, &image);
}
Result<int> bakeTerrainGrassFoliage(graphics::GrassField& field, const PointSet& points, const std::string& asset,
                                    float width, float height, const image::ImageData& albedo,
                                    const image::ImageData& normal, const image::ImageData& mask,
                                    const graphics::grass::GrassFoliageSettings& settings) {
    if (asset.empty())
        return Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "terrain.grass: asset selector required"));
    std::vector<graphics::grass::Point> roots;
    for (int i = 0; i < points.getCount(); ++i) {
        if (points.getStringAttribute(i, "asset", "") != asset) continue;
        const auto& point = points.points()[size_t(i)];
        if (point.id == 0 || point.scaleX != point.scaleZ || point.scaleX <= 0.001F)
            return Result<int>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument,
                "terrain.grass: stable ID and equal positive horizontal X/Z scales above 0.001 required"));
        graphics::grass::Point root;
        root.position = {point.x, point.y, point.z}; root.scale = point.scaleY; root.widthScale = point.scaleX;
        root.id = uint32_t((point.id ^ (point.id >> 32)) & 0xffffffu);
        root.tint = {point.colorR, point.colorG, point.colorB}; roots.push_back(root);
    }
    return field.bakeFoliagePoints(roots, width, height, albedo, normal, mask, settings);
}
void exposeTerrainGrassAdapter(ssq::Table& table) {
    table.addFunc("bakeTerrainGrassFoliage", [vm = table.getHandle()](graphics::GrassField* field,
        const PointSet* points, std::string asset, float width, float height, const image::ImageData* albedo,
        const image::ImageData* normal, const image::ImageData* mask,
        const graphics::grass::GrassFoliageSettings* settings) {
        auto result = field && points && albedo && normal && mask && settings
            ? bakeTerrainGrassFoliage(*field, *points, asset, width, height, *albedo, *normal, *mask, *settings)
            : Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                     "terrain.grass: field, points, maps and settings required"));
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("bakeTerrainGrassImage", [vm = table.getHandle()](graphics::GrassField* field, const PointSet* points,
                                                                    std::string asset, float width, float height,
                                                                    const image::ImageData* image) {
        auto result = field && points && image
                          ? bakeTerrainGrassImage(*field, *points, asset, width, height, *image)
                          : Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                   "terrain.grass: field, points and image required"));
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
    table.addFunc("bakeTerrainGrass", [vm = table.getHandle()](graphics::GrassField* field, const PointSet* points,
                                                               std::string asset, float width, float height) {
        auto result = field && points
                          ? bakeTerrainGrass(*field, *points, asset, width, height)
                          : Result<int>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                   "terrain.grass: field and points required"));
        return eve::script::projectResult(vm, std::move(result), [](int n) { return eve::Value(n); });
    });
}
}  // namespace eve::procgen
