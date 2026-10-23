#include "procgen/road/RoadRecipes.h"
#include "procgen/road/BridgeGenerator.h"
#include "procgen/road/RoadNetworkCodec.h"

#include "image/ImageData.h"
#include "procgen/ParamSchema.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/texture/TextureRecipe.h"

#include <algorithm>
#include <cmath>

namespace eve::procgen::road {

bool generateRoadNetworkMesh(const Params& params, MeshBuild& out, std::string& error) {
    auto baked = bakeRoadNetworkRecipe(params);
    if (!baked.ok()) {
        error = baked.status().describe();
        return false;
    }
    out = std::move(baked.value().mesh);
    return true;
}

Result<RoadOverlay> generateRoadNetworkOverlay(const Params& params) {
    auto baked = bakeRoadNetworkRecipe(params);
    if (!baked.ok()) return Result<RoadOverlay>::failure(baked.status());
    return Result<RoadOverlay>::success(std::move(baked.value().overlay));
}

std::unique_ptr<image::ImageData> generateRoadMarkingsTexture(const Params& params, std::string& error) {
    const int width = std::clamp(params.getWidth() > 0 ? params.getWidth() : params.getInt("deckWidth", 256), 8, 2048);
    const int   height = std::clamp(params.getHeight() > 0 ? params.getHeight() : params.getInt("height", 64), 8, 2048);
    const float dashPeriod = std::max(0.05f, params.getFloat("dashPeriod", 0.18f));
    const float dashDuty   = std::clamp(params.getFloat("dashDuty", 0.45f), 0.05f, 0.95f);
    const bool  zebra      = params.getBool("zebra", true);

    auto image = std::make_unique<image::ImageData>(width, height, "RGBA8");
    for (int y = 0; y < height; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
        for (int x = 0; x < width; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
            float       r = 0.18f, g = 0.18f, b = 0.20f;

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
                                {"straight", "curve", "bridge", "cross", "tee", "y", "fork", "skew", "t-junction",
                                 "y-junction", "sloped-t", "curve-uphill", "tight-turn", "roundabout", "interchange"}));
    schema.params.push_back(ParamDescriptor::floating("span", "Span", 36.f, 16.f, 256.f, 1.f));
    schema.params.push_back(ParamDescriptor::floating("bridgeHeight", "Bridge Height", 6.f, 1.f, 64.f, 0.5f));
    schema.params.push_back(ParamDescriptor::integer("lanes", "Lanes", 2, 1, 4));
    schema.params.push_back(ParamDescriptor::integer("pathSegments", "Path Segments", 32, 8, 256));
    schema.params.push_back(ParamDescriptor::boolean("piers", "Piers", true));
    schema.params.push_back(ParamDescriptor::boolean("markings", "Markings", true));
    schema.params.push_back(ParamDescriptor::boolean("navigation", "Navigation Overlay", true));
    schema.params.push_back(ParamDescriptor::boolean("junctions", "Junctions", true));
    schema.params.push_back(ParamDescriptor::boolean("placements", "Decoration Placements", true));
    schema.params.push_back(
        ParamDescriptor::floating("sideObjectSpacing", "Side Object Spacing", 8.f, 0.25f, 100.f, 0.25f));
    schema.params.push_back(ParamDescriptor::floating("sideObjectOffset", "Side Object Offset", 0.5f, 0.f, 32.f, 0.1f));
    schema.params.push_back(
        ParamDescriptor::floating("sideObjectClearance", "Side Object Clearance", 1.5f, 0.f, 32.f, 0.1f));
    schema.params.push_back(
        ParamDescriptor::floating("junctionChordError", "Junction Curve Error", 0.10f, 0.01f, 1.f, 0.01f));
    schema.params.push_back(
        ParamDescriptor::floating("terrainBlendDistance", "Terrain Surface Blend", 2.f, 0.f, 64.f, 0.25f));
    schema.params.push_back(ParamDescriptor::floating("chunkSize", "Render Chunk Size", 64.f, 4.f, 1024.f, 1.f));
    schema.params.push_back(ParamDescriptor::integer("lodCount", "LOD Count", 3, 1, 4));
    registry.registerRecipe(std::move(schema), generateRoadNetworkMesh);

    RecipeDescriptor bridge{"mesh.autoBridge", "Automatic Bridge", "Mesh", {}};
    auto             reference = bridgePreset(BridgeKind::Beam);
    const auto&      defaults  = reference.value();
    for (const auto& axis : {"X", "Y", "Z"}) {
        const std::string suffix(axis);
        bridge.params.push_back(ParamDescriptor::floating(
            "start" + suffix, "Start " + suffix,
            suffix == "X" ? defaults.start.x : (suffix == "Y" ? defaults.start.y : defaults.start.z), -10000.f, 10000.f,
            .5f));
        bridge.params.push_back(ParamDescriptor::floating(
            "end" + suffix, "End " + suffix,
            suffix == "X" ? defaults.end.x : (suffix == "Y" ? defaults.end.y : defaults.end.z), -10000.f, 10000.f,
            .5f));
    }
    bridge.params.push_back(ParamDescriptor::floating("deckWidth", "Driving Width", defaults.width, 1.f, 32.f, .5f));
    bridge.params.push_back(ParamDescriptor::choice("bridgeType", "Bridge Type", "beam",
                                                    {"beam", "slab", "arch", "tiedArch", "throughTruss", "deckTruss",
                                                     "suspension", "cableStayed", "cantilever", "trestle"}));
    bridge.params.push_back(ParamDescriptor::integer("spans", "Spans / Bents", defaults.spans, 1, 64));
    bridge.params.push_back(ParamDescriptor::integer("panels", "Structural Panels", defaults.panels, 2, 64));
    bridge.params.push_back(
        ParamDescriptor::integer("curveSegments", "Curve Segments", defaults.curveSegments, 4, 128));
    bridge.params.push_back(ParamDescriptor::integer("cableSides", "Cable Sides", defaults.cableSides, 3, 16));
    bridge.params.push_back(
        ParamDescriptor::floating("groundY", "Foundation Level", defaults.groundY, -10000.f, 10000.f, .5f));
    bridge.params.push_back(
        ParamDescriptor::floating("memberWidth", "Structural Member Width", defaults.memberWidth, .01f, 10.f, .05f));
    bridge.params.push_back(ParamDescriptor::floating("archRise", "Arch Rise", defaults.archRise, .1f, 100.f, .5f));
    bridge.params.push_back(
        ParamDescriptor::floating("towerHeight", "Tower Height", defaults.towerHeight, 1.f, 100.f, .5f));
    bridge.params.push_back(
        ParamDescriptor::floating("trussHeight", "Truss Height", defaults.trussHeight, .1f, 100.f, .5f));
    bridge.params.push_back(
        ParamDescriptor::floating("cableRadius", "Cable Radius", defaults.cableRadius, .01f, 2.f, .01f));
    bridge.params.push_back(
        ParamDescriptor::floating("cableSag", "Cable Sag Ratio", defaults.cableSag, .05f, .95f, .05f));
    bridge.params.push_back(
        ParamDescriptor::floating("towerInset", "Tower Inset Ratio", defaults.towerInset, .15f, .4f, .01f));
    bridge.params.push_back(ParamDescriptor::floating("towerTopWidthRatio", "Tower Top Width Ratio",
                                                      defaults.towerTopWidthRatio, .05f, 1.f, .05f));
    bridge.params.push_back(ParamDescriptor::boolean("railings", "Railings", true));
    bridge.params.push_back(ParamDescriptor::boolean("crossBracing", "Cross Bracing", true));
    bridge.params.push_back(
        ParamDescriptor::floating("deckThickness", "Deck Thickness", defaults.deckThickness, .1f, 5.f, .05f));
    bridge.params.push_back(ParamDescriptor::floating("pierWidth", "Pier Width", defaults.pierWidth, .1f, 10.f, .1f));
    bridge.params.push_back(ParamDescriptor::floating("pierDepth", "Pier Depth", defaults.pierDepth, .1f, 10.f, .1f));
    bridge.params.push_back(
        ParamDescriptor::floating("railingHeight", "Railing Height", defaults.railingHeight, .1f, 3.f, .1f));
    bridge.params.push_back(
        ParamDescriptor::floating("railingWidth", "Railing Width", defaults.railingWidth, .1f, 2.f, .05f));
    bridge.params.push_back(
        ParamDescriptor::floating("sidewalkWidth", "Sidewalk Width", defaults.sidewalkWidth, 0.f, 5.f, .1f));
    bridge.params.push_back(ParamDescriptor::boolean("piers", "Piers", true));
    bridge.params.push_back(ParamDescriptor::boolean("markings", "Markings", true));
    bridge.params.push_back(ParamDescriptor::choice("railingType", "Railing Construction", "metal",
                                                    {"metal", "concrete", "masonry", "timber"}));
    bridge.params.push_back(ParamDescriptor::boolean("connectionDetails", "Gusset Plates and Bolts", true));
    bridge.params.push_back(ParamDescriptor::integer("railingBars", "Horizontal Rails", defaults.railingBars, 1, 8));
    bridge.params.push_back(ParamDescriptor::integer("plankCount", "Timber Deck Planks", defaults.plankCount, 8, 2048));
    bridge.params.push_back(
        ParamDescriptor::floating("memberDepth", "Structural Section Depth", defaults.memberDepth, 0.01f, 10.f, 0.05f));
    bridge.params.push_back(ParamDescriptor::floating("girderHeight", "Longitudinal Girder Height",
                                                      defaults.girderHeight, 0.1f, 10.f, 0.1f));
    bridge.params.push_back(
        ParamDescriptor::floating("archRibWidth", "Arch Rib Width", defaults.archRibWidth, 0.1f, 10.f, 0.1f));
    bridge.params.push_back(
        ParamDescriptor::floating("archRibDepth", "Arch Rib Depth", defaults.archRibDepth, 0.1f, 10.f, 0.1f));
    bridge.params.push_back(ParamDescriptor::floating("foundationHeight", "Foundation Step Height",
                                                      defaults.foundationHeight, 0.1f, 5.f, 0.1f));
    bridge.params.push_back(ParamDescriptor::floating("foundationScale", "Foundation Width Scale",
                                                      defaults.foundationScale, 1.f, 5.f, 0.1f));
    bridge.params.push_back(
        ParamDescriptor::floating("pierCapHeight", "Pier Capital Height", defaults.pierCapHeight, 0.1f, 5.f, 0.1f));
    bridge.params.push_back(
        ParamDescriptor::floating("abutmentDepth", "Abutment Depth", defaults.abutmentDepth, 0.1f, 10.f, 0.1f));
    bridge.params.push_back(ParamDescriptor::floating("wingWallLength", "Retaining Wing Length",
                                                      defaults.wingWallLength, 0.1f, 20.f, 0.5f));
    bridge.params.push_back(
        ParamDescriptor::floating("uvMeters", "Texture Repeat Metres", defaults.uvMeters, 0.01f, 20.f, 0.1f));
    bridge.params.push_back(
        ParamDescriptor::floating("portalInset", "Sloped Truss End Ratio", defaults.portalInset, 0.f, 0.2f, 0.01f));
    bridge.params.push_back(ParamDescriptor::floating("cantileverCenterRatio", "Suspended Center Depth Ratio",
                                                      defaults.cantileverCenterRatio, 0.1f, 0.7f, 0.05f));
    bridge.params.push_back(
        ParamDescriptor::floating("towerLegWidth", "Tower Leg Width", defaults.towerLegWidth, 0.1f, 10.f, 0.1f));
    registry.registerRecipe(std::move(bridge), [](const Params& params, MeshBuild& out, std::string& error) {
        if (params.has("networkSnapshot")) {
            error = "mesh.autoBridge accepts endpoints, not networkSnapshot";
            return false;
        }
        auto decoded = bridgeParametersFromParams(params);
        if (!decoded.ok()) {
            error = decoded.status().describe();
            return false;
        }
        auto plan = planBridge(decoded.value());
        if (!plan.ok()) {
            error = plan.status().describe();
            return false;
        }
        auto mesh = bakeBridge(plan.value());
        if (!mesh.ok()) {
            error = mesh.status().describe();
            return false;
        }
        out = std::move(mesh).takeValue();
        return true;
    });
}

void registerRoadTextureRecipes(TextureRecipeRegistry& registry) {
    RecipeDescriptor schema = RecipeDescriptor::grid("tex.roadMarkings", "Road Markings Logic", "Road", 8, 8);
    schema.params.push_back(ParamDescriptor::floating("dashPeriod", "Dash Period", 0.18f, 0.02f, 1.f, 0.01f));
    schema.params.push_back(ParamDescriptor::floating("dashDuty", "Dash Duty", 0.45f, 0.05f, 0.95f, 0.01f));
    schema.params.push_back(ParamDescriptor::boolean("zebra", "Zebra Ends", true));
    registry.registerRecipe(std::move(schema), generateRoadMarkingsTexture);
}

}  // namespace eve::procgen::road
