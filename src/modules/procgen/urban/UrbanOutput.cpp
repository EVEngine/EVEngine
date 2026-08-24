#include "procgen/urban/UrbanOutput.h"

#include "procgen/GeneratorRegistry.h"
#include "procgen/Grid2D.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/Semantic.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/urban/UrbanGenerator.h"
#include "procgen/urban/UrbanGeometry.h"
#include "procgen/urban/UrbanTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace eve::procgen::urban {
namespace {

constexpr double kPi = 3.14159265358979323846;

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::string              cur;
    for (const char c : s) {
        if (c == sep) {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

bool parsePoints(const std::string& text, Polygon& out) {
    out.clear();
    for (const std::string& pair : split(text, ';')) {
        const auto xy = split(pair, ',');
        if (xy.size() != 2) return false;
        try {
            out.push_back({std::stod(xy[0]), std::stod(xy[1])});
        } catch (...) {
            return false;
        }
    }
    return out.size() >= 3;
}

bool makePresetLand(const std::string& preset, double w, double h, Polygon& out) {
    out.clear();
    const std::string p = preset.empty() ? "rect" : preset;
    if (p == "rect") {
        out = {{0, 0}, {w, 0}, {w, h}, {0, h}};
    } else if (p == "triangle") {
        out = {{0, 0}, {w, 0}, {w * 0.5, h}};
    } else if (p == "ellipse" || p == "circle") {
        const int    segs = 28;
        const double rx   = w * 0.5;
        const double ry   = h * 0.5;
        for (int i = 0; i < segs; ++i) {
            const double a = double(i) * 2.0 * kPi / double(segs);
            out.push_back({w * 0.5 + rx * std::cos(a), h * 0.5 + ry * std::sin(a)});
        }
    } else if (p == "l") {
        out = {{0, 0}, {w, 0}, {w, h * 0.55}, {w * 0.45, h * 0.55}, {w * 0.45, h}, {0, h}};
    } else if (p == "hexagon") {
        const double cx = w * 0.5;
        const double cy = h * 0.5;
        for (int i = 0; i < 6; ++i) {
            const double a = -kPi * 0.5 + double(i) * kPi / 3.0;
            out.push_back({cx + w * 0.5 * std::cos(a), cy + h * 0.5 * std::sin(a)});
        }
    } else {
        return false;
    }
    return out.size() >= 3;
}

void addQuad(MeshBuild& m, const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d, float y, float uScale) {
    const int   base = m.getVertexCount();
    const float ax = float(a.x), az = float(a.y);
    const float bx = float(b.x), bz = float(b.y);
    const float cx = float(c.x), cz = float(c.y);
    const float dx = float(d.x), dz = float(d.y);
    m.addVertex(ax, y, az, 0.f, 1.f, 0.f, ax * uScale, az * uScale);
    m.addVertex(bx, y, bz, 0.f, 1.f, 0.f, bx * uScale, bz * uScale);
    m.addVertex(cx, y, cz, 0.f, 1.f, 0.f, cx * uScale, cz * uScale);
    m.addVertex(dx, y, dz, 0.f, 1.f, 0.f, dx * uScale, dz * uScale);
    m.addTriangle(uint32_t(base), uint32_t(base + 1), uint32_t(base + 2));
    m.addTriangle(uint32_t(base), uint32_t(base + 2), uint32_t(base + 3));
}

}  // namespace

bool parseUrbanOptions(const Params& params, UrbanOptions& opts, std::string& error) {
    opts                        = UrbanOptions{};
    opts.seed                   = params.getSeed();
    const double      w         = std::max(1.0, double(params.getFloat("landWidth", 100.f)));
    const double      h         = std::max(1.0, double(params.getFloat("landHeight", 60.f)));
    const std::string landParam = params.getString("land", "rect");
    std::string       landText  = params.getString("landPoints", "");
    if (landText.empty()) landText = landParam;
    if (!makePresetLand(landParam, w, h, opts.land)) {
        if (!parsePoints(landText, opts.land)) {
            error = "urban: unknown land preset '" + landParam +
                    "' (use rect|triangle|ellipse|l|hexagon or explicit 'x,y;x,y;...' points)";
            return false;
        }
    }
    ensureCCW(opts.land);
    opts.minParcelArea = std::max(0.01, double(params.getFloat("minParcelArea", 4.f)));
    opts.targetParcels = std::max(0, params.getInt("targetParcels", 120));
    opts.maxLevels     = std::max(1, params.getInt("maxLevels", 10));

    opts.lambdaSize             = std::max(0.0, double(params.getFloat("lambdaSize", 0.3f)));
    opts.lambdaRegu             = std::max(0.0, double(params.getFloat("lambdaRegu", 0.5f)));
    opts.lambdaAcce             = std::max(0.0, double(params.getFloat("lambdaAcce", 0.2f)));
    opts.lambdaOrient           = std::max(0.0, double(params.getFloat("lambdaOrient", 0.f)));
    opts.gammaAngle             = std::clamp(double(params.getFloat("gammaAngle", 0.75f)), 0.0, 100.0);
    opts.gammaSide              = std::clamp(double(params.getFloat("gammaSide", 0.25f)), 0.0, 100.0);
    opts.accessThreshold        = std::clamp(double(params.getFloat("accessThreshold", 0.5f)), 0.01, 10.0);
    opts.shortEdgeFactor        = std::clamp(double(params.getFloat("shortEdgeFactor", 0.2f)), 0.0, 1.0);
    opts.streetWidth            = std::max(0.1, double(params.getFloat("streetWidth", 1.f)));
    opts.dijkstraJunctionWeight = std::max(0.0, double(params.getFloat("dijkstraJunctionWeight", 1.5f)));
    opts.boundaryStreetFraction = std::clamp(double(params.getFloat("boundaryStreetFraction", 0.5f)), 0.0, 1.0);

    const std::string pattern = params.getString("streetPattern", "default");
    if (pattern == "loop")
        opts.streetPattern = 1;
    else if (pattern == "culdesac")
        opts.streetPattern = 2;
    else if (pattern == "tree")
        opts.streetPattern = 3;
    else
        opts.streetPattern = 0;
    opts.culDeSacAfterLevel = std::max(1, params.getInt("culDeSacAfterLevel", 4));

    const std::string orient = params.getString("orientation", "none");
    opts.orientation         = orient == "east-west" ? 1 : orient == "north-south" ? 2 : 0;

    const std::string boundary = params.getString("boundaryStreet", "all");
    opts.boundaryStreetMode    = boundary == "none" ? 1 : boundary == "random" ? 2 : 0;

    opts.optimize           = params.getInt("optimize", 1) != 0;
    opts.optimizeIterations = std::clamp(params.getInt("optimizeIterations", 160), 0, 2000);
    opts.optRegu            = std::max(0.0, double(params.getFloat("optRegu", 0.2f)));
    opts.optSide            = std::max(0.0, double(params.getFloat("optSide", 1.f)));
    opts.optStre            = std::max(0.0, double(params.getFloat("optStre", 1.f)));
    opts.optJunc            = std::max(0.0, double(params.getFloat("optJunc", 0.5f)));
    opts.optClose           = std::max(0.0, double(params.getFloat("optClose", 0.3f)));
    return true;
}

bool generateUrbanGrid(const Params& params, Grid2D& out, std::string& error) {
    UrbanOptions opts;
    if (!parseUrbanOptions(params, opts, error)) return false;
    UrbanGenerator gen(opts);
    if (!gen.generate(&error)) return false;
    const UrbanLayout& layout = gen.layout();
    if (layout.parcels.empty()) {
        error = "urban.parcels: no parcels generated";
        return false;
    }

    double minX = opts.land[0].x, minY = opts.land[0].y;
    double maxX = opts.land[0].x, maxY = opts.land[0].y;
    for (const Vec2& p : opts.land) {
        minX = std::min(minX, p.x);
        minY = std::min(minY, p.y);
        maxX = std::max(maxX, p.x);
        maxY = std::max(maxY, p.y);
    }
    const double landW      = std::max(1e-6, maxX - minX);
    const double landH      = std::max(1e-6, maxY - minY);
    const double cellSize   = std::max(0.05, double(params.getFloat("cellSize", 1.f)));
    const int    gridW      = std::clamp(int(std::ceil(landW / cellSize)) + 2, 3, 1024);
    const int    gridH      = std::clamp(int(std::ceil(landH / cellSize)) + 2, 3, 1024);
    const double ox         = minX - cellSize;
    const double oy         = minY - cellSize;
    const double halfStreet = opts.streetWidth * 0.5;

    out.resize(gridW, gridH);
    out.fill(Semantic::Wall);
    for (int gy = 0; gy < gridH; ++gy) {
        for (int gx = 0; gx < gridW; ++gx) {
            const Vec2 c{ox + (double(gx) + 0.5) * cellSize, oy + (double(gy) + 0.5) * cellSize};
            if (!pointInPolygon(c, opts.land)) continue;
            bool onStreet = false;
            for (const Street& s : layout.streets) {
                for (size_t i = 1; i < s.pts.size(); ++i) {
                    if (distanceToSegment(c, s.pts[i - 1], s.pts[i]) <= halfStreet) {
                        onStreet = true;
                        break;
                    }
                }
                if (onStreet) break;
            }
            if (onStreet) {
                out.setCell(gx, gy, int(Semantic::Road));
                continue;
            }
            int pid = 0;
            for (size_t p = 0; p < layout.parcels.size(); ++p) {
                Polygon poly;
                for (const int ci : layout.parcels[p].ring) poly.push_back(layout.corners[size_t(ci)]);
                if (pointInPolygon(c, poly)) {
                    pid = int(p) + 1;
                    break;
                }
            }
            out.setCell(gx, gy, pid > 0 ? int(Semantic::Floor) : int(Semantic::Wall));
            if (pid > 0) out.setDetail(gx, gy, std::min(pid, 254));
        }
    }

    // Parcel anchors for building placement.
    out.clearObjects();
    for (size_t p = 0; p < layout.parcels.size(); ++p) {
        Polygon poly;
        for (const int ci : layout.parcels[p].ring) poly.push_back(layout.corners[size_t(ci)]);
        const Vec2 c = centroid(poly);
        out.addObjectAt("parcel" + std::to_string(p), "parcel", float((c.x - ox) / cellSize),
                        float((c.y - oy) / cellSize));
    }

    out.setMeta("algorithm", "urban.parcels");
    out.setMeta("seed", std::to_string(opts.seed));
    out.setMeta("parcels", std::to_string(layout.parcels.size()));
    out.setMeta("streets", std::to_string(layout.streets.size()));
    out.setMeta("junctions", std::to_string(layout.streetJunctions));
    out.setMeta("streetLength", std::to_string(layout.totalStreetLength));
    out.setMeta("avgIrregularity", std::to_string(layout.avgIrregularity));
    out.setMeta("levels", std::to_string(layout.levelsUsed));
    out.setMeta("streetPattern", params.getString("streetPattern", "default"));
    out.setMeta("optimize", opts.optimize ? "1" : "0");
    out.setMeta("cellSize", std::to_string(cellSize));
    return true;
}

bool generateUrbanMesh(const Params& params, MeshBuild& out, std::string& error) {
    UrbanOptions opts;
    if (!parseUrbanOptions(params, opts, error)) return false;
    UrbanGenerator gen(opts);
    if (!gen.generate(&error)) return false;
    const UrbanLayout& layout = gen.layout();
    if (layout.parcels.empty()) {
        error = "mesh.urban: no parcels generated";
        return false;
    }

    out.clear();
    const float extrude = std::max(0.f, params.getFloat("extrude", 0.f));
    const float uvScale = std::max(0.001f, params.getFloat("uvScale", 0.1f));

    // Street ribbons (slightly above the ground plane).
    for (const Street& s : layout.streets) {
        if (s.pts.size() < 2) continue;
        const double half = s.width * 0.5;
        for (size_t i = 1; i < s.pts.size(); ++i) {
            const Vec2 dir  = normalize(s.pts[i] - s.pts[i - 1]);
            const Vec2 perp = perpendicular(dir);
            const Vec2 l0   = s.pts[i - 1] + perp * half;
            const Vec2 r0   = s.pts[i - 1] - perp * half;
            const Vec2 l1   = s.pts[i] + perp * half;
            const Vec2 r1   = s.pts[i] - perp * half;
            addQuad(out, l0, l1, r1, r0, 0.01f, uvScale);
        }
    }

    for (size_t p = 0; p < layout.parcels.size(); ++p) {
        Polygon poly;
        for (const int ci : layout.parcels[p].ring) poly.push_back(layout.corners[size_t(ci)]);
        std::vector<int> tris;
        if (!triangulatePolygon(poly, tris)) {
            error = "mesh.urban: failed to triangulate parcel " + std::to_string(p);
            return false;
        }
        auto emitVert = [&](const Vec2& v, float y, float nx, float ny, float nz) {
            out.addVertex(float(v.x), y, float(v.y), nx, ny, nz, float(v.x) * uvScale, float(v.y) * uvScale);
        };
        if (extrude <= 0.f) {
            const int base = out.getVertexCount();
            for (size_t i = 0; i < poly.size(); ++i) emitVert(poly[i], 0.f, 0.f, 1.f, 0.f);
            for (size_t t = 0; t + 2 < tris.size(); t += 3)
                // Ear-clipping emits CCW (x,y) which is -Y in the XZ plane; flip for +Y.
                out.addTriangle(uint32_t(base + tris[t]), uint32_t(base + tris[t + 2]), uint32_t(base + tris[t + 1]));
        } else {
            const double yTop    = double(extrude);
            const int    baseTop = out.getVertexCount();
            for (size_t i = 0; i < poly.size(); ++i) emitVert(poly[i], float(yTop), 0.f, 1.f, 0.f);
            for (size_t t = 0; t + 2 < tris.size(); t += 3)
                out.addTriangle(uint32_t(baseTop + tris[t]), uint32_t(baseTop + tris[t + 2]),
                                uint32_t(baseTop + tris[t + 1]));
            const int baseBottom = out.getVertexCount();
            for (size_t i = 0; i < poly.size(); ++i) emitVert(poly[i], 0.f, 0.f, -1.f, 0.f);
            for (size_t t = 0; t + 2 < tris.size(); t += 3)
                out.addTriangle(uint32_t(baseBottom + tris[t]), uint32_t(baseBottom + tris[t + 1]),
                                uint32_t(baseBottom + tris[t + 2]));
            const size_t m = poly.size();
            for (size_t i = 0; i < m; ++i) {
                const Vec2& a    = poly[i];
                const Vec2& b    = poly[(i + 1) % m];
                const Vec2  n    = normalize(perpendicular(b - a));
                const int   base = out.getVertexCount();
                emitVert(a, 0.f, float(n.x), 0.f, float(n.y));
                emitVert(b, 0.f, float(n.x), 0.f, float(n.y));
                emitVert(b, float(yTop), float(n.x), 0.f, float(n.y));
                emitVert(a, float(yTop), float(n.x), 0.f, float(n.y));
                out.addTriangle(uint32_t(base), uint32_t(base + 1), uint32_t(base + 2));
                out.addTriangle(uint32_t(base), uint32_t(base + 2), uint32_t(base + 3));
            }
        }
    }

    out.setMeta("algorithm", "mesh.urban");
    out.setMeta("seed", std::to_string(opts.seed));
    out.setMeta("parcels", std::to_string(layout.parcels.size()));
    out.setMeta("streets", std::to_string(layout.streets.size()));
    out.setMeta("junctions", std::to_string(layout.streetJunctions));
    out.setMeta("streetLength", std::to_string(layout.totalStreetLength));
    out.setMeta("avgIrregularity", std::to_string(layout.avgIrregularity));
    out.setMeta("extrude", std::to_string(extrude));
    return true;
}

static RecipeDescriptor makeUrbanDescriptor(std::string id, std::string name,
                                            std::string category) {
    RecipeDescriptor descriptor{std::move(id), std::move(name), std::move(category), {}};
    descriptor.params.push_back(ParamDescriptor::integer("seed", "Seed", 1, 0, 2147483647));
    descriptor.params.push_back(ParamDescriptor::choice("land", "Land Shape", "rect",
                                                        {"rect", "triangle", "ellipse", "l", "hexagon"}));
    descriptor.params.push_back(ParamDescriptor::floating("landWidth", "Land Width", 100.f, 1.f, 10000.f, 1.f));
    descriptor.params.push_back(ParamDescriptor::floating("landHeight", "Land Height", 60.f, 1.f, 10000.f, 1.f));
    descriptor.params.push_back(ParamDescriptor::text("landPoints", "Custom Land Points", ""));
    descriptor.params.push_back(ParamDescriptor::integer("targetParcels", "Target Parcels", 120, 0, 10000));
    descriptor.params.push_back(ParamDescriptor::floating("minParcelArea", "Minimum Parcel Area", 4.f, 0.01f,
                                                          10000.f, 0.1f));
    descriptor.params.push_back(ParamDescriptor::integer("maxLevels", "Maximum Levels", 10, 1, 128));
    descriptor.params.push_back(ParamDescriptor::choice("streetPattern", "Street Pattern", "default",
                                                        {"default", "loop", "culdesac", "tree"}));
    descriptor.params.push_back(ParamDescriptor::floating("streetWidth", "Street Width", 1.f, 0.1f, 100.f,
                                                          0.1f));
    descriptor.params.push_back(ParamDescriptor::choice("orientation", "Orientation", "none",
                                                        {"none", "east-west", "north-south"}));
    descriptor.params.push_back(ParamDescriptor::choice("boundaryStreet", "Boundary Streets", "all",
                                                        {"all", "none", "random"}));
    descriptor.params.push_back(ParamDescriptor::floating("cellSize", "Grid Cell Size", 1.f, 0.05f, 100.f,
                                                          0.05f));
    descriptor.params.push_back(ParamDescriptor::boolean("optimize", "Optimize", true));
    auto addAdvanced = [&](ParamDescriptor param) {
        param.advanced = true;
        descriptor.params.push_back(std::move(param));
    };
    addAdvanced(ParamDescriptor::floating("lambdaSize", "Size Weight", 0.3f, 0.f, 100.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("lambdaRegu", "Regularity Weight", 0.5f, 0.f, 100.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("lambdaAcce", "Access Weight", 0.2f, 0.f, 100.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("lambdaOrient", "Orientation Weight", 0.f, 0.f, 100.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("gammaAngle", "Angle Weight", 0.75f, 0.f, 100.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("gammaSide", "Side Weight", 0.25f, 0.f, 100.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("accessThreshold", "Access Threshold", 0.5f, 0.01f, 10.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("shortEdgeFactor", "Short Edge Factor", 0.2f, 0.f, 1.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("dijkstraJunctionWeight", "Junction Weight", 1.5f, 0.f, 100.f,
                                          0.1f));
    addAdvanced(ParamDescriptor::floating("boundaryStreetFraction", "Boundary Street Fraction", 0.5f, 0.f,
                                          1.f, 0.01f));
    addAdvanced(ParamDescriptor::integer("culDeSacAfterLevel", "Cul-de-sac Level", 4, 1, 128));
    addAdvanced(ParamDescriptor::integer("optimizeIterations", "Optimize Iterations", 160, 0, 2000));
    addAdvanced(ParamDescriptor::floating("optRegu", "Optimize Regularity", 0.2f, 0.f, 100.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("optSide", "Optimize Sides", 1.f, 0.f, 100.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("optStre", "Optimize Streets", 1.f, 0.f, 100.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("optJunc", "Optimize Junctions", 0.5f, 0.f, 100.f, 0.01f));
    addAdvanced(ParamDescriptor::floating("optClose", "Optimize Closure", 0.3f, 0.f, 100.f, 0.01f));
    return descriptor;
}

void registerUrbanGenerators(GeneratorRegistry& registry) {
    registry.registerAlgorithm(
        makeUrbanDescriptor("urban.parcels", "Urban Parcels", "Urban"), generateUrbanGrid);
}

void registerUrbanMeshRecipes(MeshRecipeRegistry& registry) {
    RecipeDescriptor descriptor =
        makeUrbanDescriptor("mesh.urban", "Urban Blocks", "Urban");
    descriptor.params.push_back(
        ParamDescriptor::floating("extrude", "Extrusion Height", 0.f, 0.f, 1000.f, 0.1f));
    descriptor.params.push_back(
        ParamDescriptor::floating("uvScale", "UV Scale", 0.1f, 0.f, 100.f, 0.01f));
    registry.registerRecipe(std::move(descriptor), generateUrbanMesh);
}

}  // namespace eve::procgen::urban
