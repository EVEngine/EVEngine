#include "procgen/heightmap/TerrainCurveTexture.h"

#include "common/SquirrelBinding.h"
#include "image/ImageData.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainRasterInternal.h"

#include <algorithm>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::procgen {
Result<TerrainCurveTextureReceipt> bakeTerrainCurveTexture(image::ImageData& output, const Heightmap& curve) {
    if (output.getWidth() <= 0 || output.getHeight() != 1 || !raster_detail::validRaster(curve) ||
        curve.getHeight() != 1)
        return Result<TerrainCurveTextureReceipt>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "terrain.curveTexture: non-empty one-row image and curve required"));
    const auto sampleCurve = [&curve](double t) {
        return raster_detail::sample(curve, std::clamp(t, 0.0, 1.0), 0.0);
    };
    const double atZero = sampleCurve(0);
    double minimum = atZero, maximum = atZero;
    image::ImageData candidate(output.getWidth(), 1, output.getFormat());
    for (int x = 1; x < output.getWidth(); ++x) {
        const double value = sampleCurve(double(x) / output.getWidth());
        if (!raster_detail::isRepresentable(value))
            return Result<TerrainCurveTextureReceipt>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "terrain.curveTexture: curve output is not representable"));
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        candidate.setPixel(x, 0, image::ImageData::Colorf{float(value), 0, 0, 0});
    }
    output.adopt(candidate);
    return Result<TerrainCurveTextureReceipt>::success(
        TerrainCurveTextureReceipt{float(minimum), float(maximum), output.getWidth()});
}

void exposeTerrainCurveTexture(ssq::Table& table) {
    table.addFunc("bakeTerrainCurveTexture", [vm = table.getHandle()](image::ImageData* output,
                                                                      const Heightmap* curve) {
        auto result = output && curve
                          ? bakeTerrainCurveTexture(*output, *curve)
                          : Result<TerrainCurveTextureReceipt>::failure(Diagnostic::error(
                                DiagnosticCode::InvalidArgument, "bakeTerrainCurveTexture requires output and curve"));
        return eve::script::projectResult(vm, std::move(result), [](const TerrainCurveTextureReceipt& receipt) {
            return eve::Value(eve::Value::Object{{"minimum", eve::Value(receipt.minimum)},
                                                 {"maximum", eve::Value(receipt.maximum)},
                                                 {"writtenPixels", eve::Value(receipt.writtenPixels)}});
        });
    });
}
}  // namespace eve::procgen
