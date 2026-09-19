#include "procgen/road/RoadRecipes.h"

#include "image/ImageData.h"
#include "procgen/ParamSchema.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/texture/TextureRecipe.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen::road {
namespace {

RoadBakeOptions bakeOptionsFromParams(const Params& params) {
    RoadBakeOptions options;
    options.pathSegmentsPerEdge = std::max(4, params.getInt("pathSegments", 48));
    options.turnSamples         = std::max(4, params.getInt("turnSamples", 12));
    options.includePiers        = params.getBool("piers", true);
    options.includeMarkings     = params.getBool("markings", true);
    options.includeNavigation   = params.getBool("navigation", true);
    options.includeJunctions    = params.getBool("junctions", true);
    return options;
}

Result<RoadNetwork> networkFromParams(const Params& params) {
    const std::string scene = params.getString("scene", "straight");
    return RoadNetwork::makeScene(scene, params.getFloat("span", 36.f), params.getFloat("bridgeHeight", 6.f),
                                  std::max(1, params.getInt("lanes", 2)), params.getSeed());
}

}  // namespace

bool generateRoadNetworkMesh(const Params& params, MeshBuild& out, std::string& error) {
    auto network = networkFromParams(params);
    if (!network.ok()) {
        error = network.status().describe();
        return false;
    }
    auto baked = bakeRoadNetwork(network.value(), bakeOptionsFromParams(params));
    if (!baked.ok()) {
        error = baked.status().describe();
        return false;
    }
    out = std::move(baked.value().mesh);
    return true;
}

Result<RoadOverlay> generateRoadNetworkOverlay(const Params& params) {
    auto network = networkFromParams(params);
    if (!network.ok()) return Result<RoadOverlay>::failure(network.status());
    auto baked = bakeRoadNetwork(network.value(), bakeOptionsFromParams(params));
    if (!baked.ok()) return Result<RoadOverlay>::failure(baked.status());
    return Result<RoadOverlay>::success(std::move(baked.value().overlay));
}

std::unique_ptr<image::ImageData> generateRoadMarkingsTexture(const Params& params, std::string& error) {
    const int width  = std::clamp(params.getWidth() > 0 ? params.getWidth() : params.getInt("width", 256), 8, 2048);
    const int height = std::clamp(params.getHeight() > 0 ? params.getHeight() : params.getInt("height", 64), 8, 2048);
    const float dashPeriod = std::max(0.05f, params.getFloat("dashPeriod", 0.18f));
    const float dashDuty   = std::clamp(params.getFloat("dashDuty", 0.45f), 0.05f, 0.95f);
    const bool  zebra      = params.getBool("zebra", true);

    auto image = std::make_unique<image::ImageData>(width, height, "RGBA8");
    for (int y = 0; y < height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
        for (int x = 0; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            float r = 0.18f, g = 0.18f, b = 0.20f;

            // White edge lines near v=0 and v=1.
            if (v < 0.08f || v > 0.92f) {
                r = g = b = 0.92f;
            }
            // Yellow dashed center.
            else if (std::fabs(v - 0.5f) < 0.035f) {
                const float phase = std::fmod(u, dashPeriod) / dashPeriod;
                if (phase < dashDuty) {
                    r = 0.95f;
                    g = 0.75f;
                    b = 0.12f;
                }
            }
            // Lane divider dashes at 1/4 and 3/4.
            else if (std::fabs(v - 0.25f) < 0.02f || std::fabs(v - 0.75f) < 0.02f) {
                const float phase = std::fmod(u + 0.05f, dashPeriod) / dashPeriod;
                if (phase < dashDuty) r = g = b = 0.88f;
            }

            // Zebra band near u ends (intersection approach).
            if (zebra && (u < 0.08f || u > 0.92f)) {
                const int stripe = static_cast<int>(v * 12.f);
                if ((stripe & 1) == 0) r = g = b = 0.95f;
            }

            // Direction arrow glyph in the middle third.
            const float au = (u - 0.42f) / 0.16f;
            const float av = (v - 0.5f) / 0.18f;
            if (au >= 0.f && au <= 1.f && std::fabs(av) <= (1.f - au) * 0.55f + 0.08f) {
                r = 0.95f;
                g = 0.45f;
                b = 0.08f;
            }

            image->setPixel(x, y, image::ImageData::Colorf{r, g, b, 1.f});
        }
    }
    (void)error;
    return image;
}

void registerRoadMeshRecipes(MeshRecipeRegistry& registry) {
    RecipeDescriptor schema{"mesh.roadNetwork", "Procedural Road Network", "Mesh", {}};
    schema.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 0, 2147483647));
    schema.params.push_back(
        ParamDescriptor::choice("scene", "Scene", "straight",
                                {"straight", "curve", "bridge", "cross", "interchange"}));
    schema.params.push_back(ParamDescriptor::floating("span", "Span", 36.f, 8.f, 256.f, 1.f));
    schema.params.push_back(ParamDescriptor::floating("bridgeHeight", "Bridge Height", 6.f, 1.f, 64.f, 0.5f));
    schema.params.push_back(ParamDescriptor::integer("lanes", "Lanes", 2, 1, 4));
    schema.params.push_back(ParamDescriptor::integer("pathSegments", "Path Segments", 32, 8, 256));
    schema.params.push_back(ParamDescriptor::boolean("piers", "Piers", true));
    schema.params.push_back(ParamDescriptor::boolean("markings", "Markings", true));
    schema.params.push_back(ParamDescriptor::boolean("navigation", "Navigation Mesh", false));
    schema.params.push_back(ParamDescriptor::boolean("junctions", "Junctions", true));
    registry.registerRecipe(std::move(schema), generateRoadNetworkMesh);
}

void registerRoadTextureRecipes(TextureRecipeRegistry& registry) {
    RecipeDescriptor schema = RecipeDescriptor::grid("tex.roadMarkings", "Road Markings Logic", "Road", 8, 8);
    schema.params.push_back(ParamDescriptor::floating("dashPeriod", "Dash Period", 0.18f, 0.02f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("dashDuty", "Dash Duty", 0.45f, 0.05f, 0.95f, 0.01f));
    schema.params.push_back(ParamDescriptor::boolean("zebra", "Zebra Ends", true));
    registry.registerRecipe(std::move(schema), generateRoadMarkingsTexture);
}

}  // namespace eve::procgen::road
