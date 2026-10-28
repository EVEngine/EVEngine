#include "procgen/algorithms/PrototypeKit.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include "procgen/ParamSchema.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/PrototypeKitPrimitives.h"

// PrototypeKit.cpp
namespace eve::procgen {
namespace {

using prototype_detail::addBox;
using prototype_detail::addCylinder;
using prototype_detail::addSphere;
using prototype_detail::addTorus;
using prototype_detail::addWedge;
using prototype_detail::Vec3;

constexpr std::array<PrototypePieceDescriptor, 75> kPieces{{
    {"arrow", "Arrow", 1.0f, 0.18f, 2.0f},
    {"box", "Open Box", 1.5f, 1.0f, 1.5f},
    {"coin", "Coin", 0.8f, 0.12f, 0.8f},
    {"cone", "Cone", 1.0f, 1.5f, 1.0f},
    {"cone1", "Cone Low", 1.2f, 0.8f, 1.2f},
    {"cone2", "Cone Tall", 0.8f, 2.0f, 0.8f},
    {"cone3", "Frustum", 1.0f, 1.5f, 1.0f},
    {"cone4", "Pyramid", 1.2f, 1.5f, 1.2f},
    {"cube", "Cube", 1.0f, 1.0f, 1.0f},
    {"cube4", "Low Block", 1.0f, 0.5f, 1.0f},
    {"cube5", "Tall Block", 1.0f, 2.0f, 1.0f},
    {"cube6", "Wide Block", 2.0f, 1.0f, 1.0f},
    {"cube7", "Platform Block", 2.0f, 0.35f, 2.0f},
    {"cube8", "Beam Block", 3.0f, 0.4f, 0.4f},
    {"cube9", "Inset Block", 1.2f, 1.2f, 1.2f},
    {"cylinder", "Cylinder", 1.0f, 1.0f, 1.0f},
    {"cylinder1", "Cylinder Detailed", 1.0f, 1.0f, 1.0f},
    {"cylinder2", "Cylinder Low", 1.3f, 0.5f, 1.3f},
    {"cylinder3", "Cylinder Tall", 0.8f, 2.0f, 0.8f},
    {"cylinder4", "Hex Cylinder", 1.0f, 1.0f, 1.0f},
    {"cylinder5", "Octagonal Column", 1.0f, 2.0f, 1.0f},
    {"door", "Door", 1.0f, 2.1f, 0.15f},
    {"door1", "Door Framed", 1.2f, 2.2f, 0.2f},
    {"door2", "Door Double", 1.8f, 2.2f, 0.18f},
    {"door3", "Door Reinforced", 1.2f, 2.2f, 0.22f},
    {"fence-edge", "Fence Edge", 0.4f, 1.2f, 0.4f},
    {"fence-wood", "Wood Fence", 2.5f, 1.2f, 0.2f},
    {"fence", "Fence", 2.5f, 1.2f, 0.16f},
    {"fence2", "Fence Dense", 2.5f, 1.4f, 0.18f},
    {"fence3", "Fence Low", 2.5f, 0.8f, 0.18f},
    {"ground-corner", "Ground Corner", 2.0f, 0.15f, 2.0f},
    {"ground", "Ground Tile", 2.0f, 0.12f, 2.0f},
    {"ground1", "Ground Slab", 2.0f, 0.3f, 2.0f},
    {"key", "Key", 1.8f, 0.12f, 0.7f},
    {"ladder", "Ladder", 1.0f, 2.5f, 0.16f},
    {"ladder1", "Wide Ladder", 1.4f, 3.0f, 0.18f},
    {"pillar", "Pillar", 0.8f, 2.5f, 0.8f},
    {"pillar1", "Pillar Square", 0.8f, 2.5f, 0.8f},
    {"pillar2", "Pillar Round", 0.8f, 2.5f, 0.8f},
    {"pillar3", "Pillar Heavy", 1.0f, 3.0f, 1.0f},
    {"pillar4", "Pillar Tapered", 0.9f, 2.5f, 0.9f},
    {"railing-edge", "Railing Edge", 0.35f, 1.0f, 0.35f},
    {"railing", "Railing", 2.5f, 1.0f, 0.12f},
    {"ramp", "Ramp", 2.0f, 1.0f, 2.0f},
    {"ramp1", "Ramp Wide", 3.0f, 1.2f, 2.0f},
    {"sphere", "Sphere", 1.0f, 1.0f, 1.0f},
    {"sphere1", "Sphere Detailed", 1.0f, 1.0f, 1.0f},
    {"sphere2", "Ellipsoid", 1.2f, 1.8f, 1.2f},
    {"spike", "Spike", 0.5f, 1.2f, 0.5f},
    {"spikes-big", "Spikes Big", 2.0f, 1.2f, 2.0f},
    {"spikes-small", "Spikes Small", 2.0f, 0.6f, 2.0f},
    {"stairs-corner", "Stairs Corner", 2.0f, 1.5f, 2.0f},
    {"stairs-corner1", "Stairs Corner Wide", 3.0f, 1.5f, 3.0f},
    {"stairs", "Stairs", 2.0f, 1.5f, 2.5f},
    {"stairs1", "Stairs Solid", 2.0f, 1.5f, 2.5f},
    {"stairs2", "Stairs Low", 2.0f, 0.8f, 2.5f},
    {"toggle-switch", "Toggle Switch", 0.8f, 0.6f, 0.5f},
    {"torus", "Torus", 1.4f, 0.35f, 1.4f},
    {"wall-corner-bottom", "Wall Corner Bottom", 2.0f, 1.0f, 2.0f},
    {"wall-corner-bottom1", "Wall Corner Bottom Thin", 2.0f, 0.6f, 2.0f},
    {"wall-corner-bottom2", "Wall Corner Bottom Heavy", 2.0f, 1.2f, 2.0f},
    {"wall-corner-top", "Wall Corner Top", 2.0f, 1.0f, 2.0f},
    {"wall-corner-top1", "Wall Corner Top Thin", 2.0f, 0.6f, 2.0f},
    {"wall-corner", "Wall Corner", 2.0f, 3.0f, 2.0f},
    {"wall-corner1", "Wall Corner Short", 2.0f, 2.0f, 2.0f},
    {"wall-corner4", "Wall Corner Heavy", 2.0f, 3.0f, 2.0f},
    {"wall-door", "Wall Door", 3.0f, 3.0f, 0.25f},
    {"wall-door1", "Wall Door Wide", 4.0f, 3.0f, 0.25f},
    {"wall-door2", "Wall Door Tall", 3.0f, 4.0f, 0.25f},
    {"wall-window", "Wall Window", 3.0f, 3.0f, 0.25f},
    {"wall-window1", "Wall Window Wide", 4.0f, 3.0f, 0.25f},
    {"wall", "Wall", 3.0f, 3.0f, 0.25f},
    {"wall1", "Wall Half", 3.0f, 1.5f, 0.25f},
    {"window", "Window", 1.2f, 1.2f, 0.15f},
    {"window1", "Window Wide", 1.8f, 1.2f, 0.15f},
}};

const PrototypePieceDescriptor* findPiece(std::string_view id) {
    const auto it = std::find_if(kPieces.begin(), kPieces.end(), [id](const auto& piece) { return piece.id == id; });
    return it == kPieces.end() ? nullptr : &*it;
}

struct BuildContext {
    MeshBuild mesh;
    float     w;
    float     h;
    float     d;
    float     t;
    int       detail;
    int       steps;
};

float positiveParam(const Params& params, const char* name, float fallback) {
    return params.has(name) ? params.getFloat(name, fallback) : fallback;
}

void addFrame(BuildContext& c, float openingWidth, float openingHeight, float sill, const char* group) {
    const float side = std::max(c.t, (c.w - openingWidth) * 0.5f);
    addBox(c.mesh, {-c.w * 0.5f + side * 0.5f, c.h * 0.5f, 0}, {side, c.h, c.d}, group);
    addBox(c.mesh, {c.w * 0.5f - side * 0.5f, c.h * 0.5f, 0}, {side, c.h, c.d}, group);
    if (sill > 0.0f) addBox(c.mesh, {0, sill * 0.5f, 0}, {openingWidth, sill, c.d}, group);
    const float top = std::max(c.t, c.h - sill - openingHeight);
    addBox(c.mesh, {0, c.h - top * 0.5f, 0}, {openingWidth, top, c.d}, group);
}

void addCorner(BuildContext& c, const char* group) {
    addBox(c.mesh, {0, c.h * 0.5f, -c.d * 0.5f + c.t * 0.5f}, {c.w, c.h, c.t}, group);
    addBox(c.mesh, {-c.w * 0.5f + c.t * 0.5f, c.h * 0.5f, 0}, {c.t, c.h, c.d}, group);
}

void addFence(BuildContext& c, int posts, int rails, bool pickets) {
    posts = std::max(2, posts);
    for (int i = 0; i < posts; ++i) {
        const float x = -c.w * 0.5f + c.w * float(i) / float(posts - 1);
        addBox(c.mesh, {x, c.h * 0.5f, 0}, {c.t, c.h, c.d}, "posts");
    }
    for (int i = 0; i < rails; ++i) {
        const float y = c.h * float(i + 1) / float(rails + 1);
        addBox(c.mesh, {0, y, 0}, {c.w, c.t, c.d * 0.75f}, "rails");
    }
    if (pickets) {
        const int count = std::max(3, posts * 2 - 1);
        for (int i = 1; i < count - 1; ++i) {
            const float x = -c.w * 0.5f + c.w * float(i) / float(count - 1);
            addBox(c.mesh, {x, c.h * 0.45f, 0}, {c.t * 0.55f, c.h * 0.9f, c.d * 0.6f}, "pickets");
        }
    }
}

void addLadder(BuildContext& c) {
    addBox(c.mesh, {-c.w * 0.5f + c.t * 0.5f, c.h * 0.5f, 0}, {c.t, c.h, c.d}, "rails");
    addBox(c.mesh, {c.w * 0.5f - c.t * 0.5f, c.h * 0.5f, 0}, {c.t, c.h, c.d}, "rails");
    for (int i = 1; i < c.steps; ++i) {
        addBox(c.mesh, {0, c.h * float(i) / float(c.steps), 0}, {c.w, c.t * 0.75f, c.d * 0.75f}, "rungs");
    }
}

void addStairs(BuildContext& c, bool corner) {
    for (int i = 0; i < c.steps; ++i) {
        const float fraction  = float(i + 1) / float(c.steps);
        const float stepDepth = c.d / float(c.steps);
        const float z         = c.d * 0.5f - stepDepth * (float(i) + 0.5f);
        addBox(c.mesh, {0, c.h * fraction * 0.5f, z}, {c.w, c.h * fraction, stepDepth}, "steps");
        if (corner) {
            const float x = c.w * 0.5f - stepDepth * (float(i) + 0.5f);
            addBox(c.mesh, {x, c.h * fraction * 0.5f, 0}, {stepDepth, c.h * fraction, c.d}, "corner-steps");
        }
    }
}

void buildPiece(std::string_view id, BuildContext& c) {
    if (id == "arrow") {
        addBox(c.mesh, {0, c.h * 0.5f, -c.d * 0.15f}, {c.w * 0.22f, c.h, c.d * 0.7f}, "shaft");
        addWedge(c.mesh, {0, c.h * 0.5f, c.d * 0.35f}, {c.w, c.h, c.d * 0.3f}, false, "head");
    } else if (id == "box") {
        addBox(c.mesh, {0, c.t * 0.5f, 0}, {c.w, c.t, c.d}, "base");
        addBox(c.mesh, {-c.w * 0.5f + c.t * 0.5f, c.h * 0.5f, 0}, {c.t, c.h, c.d}, "walls");
        addBox(c.mesh, {c.w * 0.5f - c.t * 0.5f, c.h * 0.5f, 0}, {c.t, c.h, c.d}, "walls");
        addBox(c.mesh, {0, c.h * 0.5f, -c.d * 0.5f + c.t * 0.5f}, {c.w, c.h, c.t}, "walls");
        addBox(c.mesh, {0, c.h * 0.5f, c.d * 0.5f - c.t * 0.5f}, {c.w, c.h, c.t}, "walls");
    } else if (id == "coin") {
        addCylinder(c.mesh, {0, c.h * 0.5f, 0}, c.w * 0.5f, c.w * 0.5f, c.h, c.detail, "coin");
    } else if (id.starts_with("cone")) {
        int         variant = id == "cone" ? 0 : id.back() - '0';
        const int   sides   = variant == 4 ? 4 : c.detail;
        const float top     = variant == 3 ? c.w * 0.22f : 0.0f;
        addCylinder(c.mesh, {0, c.h * 0.5f, 0}, c.w * 0.5f, top, c.h, sides, "body");
    } else if (id.starts_with("cube")) {
        addBox(c.mesh, {0, c.h * 0.5f, 0}, {c.w, c.h, c.d}, "body");
        if (id == "cube9") addBox(c.mesh, {0, c.h + c.t * 0.25f, 0}, {c.w * 0.72f, c.t * 0.5f, c.d * 0.72f}, "inset");
    } else if (id.starts_with("cylinder")) {
        const int variant = id == "cylinder" ? 0 : id.back() - '0';
        const int sides   = variant == 4 ? 6 : (variant == 5 ? 8 : c.detail + (variant == 1 ? 8 : 0));
        addCylinder(c.mesh, {0, c.h * 0.5f, 0}, c.w * 0.5f, c.w * 0.5f, c.h, sides, "body");
    } else if (id.starts_with("door")) {
        addBox(c.mesh, {0, c.h * 0.5f, 0}, {c.w, c.h, c.d}, "panel");
        if (id != "door") {
            addFrame(c, c.w - 2.0f * c.t, c.h - 2.0f * c.t, 0.0f, "frame");
            if (id == "door3") addBox(c.mesh, {0, c.h * 0.55f, c.d}, {c.w * 0.9f, c.t, c.t}, "brace");
        }
    } else if (id.starts_with("fence")) {
        const bool edge = id == "fence-edge";
        if (edge)
            addBox(c.mesh, {0, c.h * 0.5f, 0}, {c.w, c.h, c.d}, "post");
        else
            addFence(c, id == "fence2" ? 7 : 5, id == "fence3" ? 1 : 2, id == "fence-wood");
    } else if (id.starts_with("ground")) {
        if (id == "ground-corner") {
            addBox(c.mesh, {0, c.h * 0.5f, -c.d * 0.25f}, {c.w, c.h, c.d * 0.5f}, "ground");
            addBox(c.mesh, {-c.w * 0.25f, c.h * 0.5f, c.d * 0.25f}, {c.w * 0.5f, c.h, c.d * 0.5f}, "ground");
        } else
            addBox(c.mesh, {0, c.h * 0.5f, 0}, {c.w, c.h, c.d}, "ground");
    } else if (id == "key") {
        addTorus(c.mesh, {-c.w * 0.35f, c.h * 0.5f, 0}, c.d * 0.28f, c.t * 0.45f, c.detail, 6, "ring");
        addBox(c.mesh, {c.w * 0.1f, c.h * 0.5f, 0}, {c.w * 0.65f, c.t, c.t}, "shaft");
        addBox(c.mesh, {c.w * 0.38f, c.h * 0.5f, c.d * 0.13f}, {c.t, c.t, c.d * 0.35f}, "teeth");
    } else if (id.starts_with("ladder")) {
        addLadder(c);
    } else if (id.starts_with("pillar")) {
        const bool square = id == "pillar1";
        if (square)
            addBox(c.mesh, {0, c.h * 0.5f, 0}, {c.w * 0.65f, c.h, c.d * 0.65f}, "shaft");
        else
            addCylinder(c.mesh, {0, c.h * 0.5f, 0}, c.w * 0.3f, id == "pillar4" ? c.w * 0.22f : c.w * 0.3f, c.h,
                        id == "pillar2" ? c.detail : 8, "shaft");
        addBox(c.mesh, {0, c.t * 0.5f, 0}, {c.w, c.t, c.d}, "base");
        addBox(c.mesh, {0, c.h - c.t * 0.5f, 0}, {c.w, c.t, c.d}, "capital");
    } else if (id.starts_with("railing")) {
        if (id == "railing-edge")
            addBox(c.mesh, {0, c.h * 0.5f, 0}, {c.w, c.h, c.d}, "post");
        else
            addFence(c, 5, 1, false);
    } else if (id.starts_with("ramp")) {
        addWedge(c.mesh, {0, c.h * 0.5f, 0}, {c.w, c.h, c.d}, false, "ramp");
    } else if (id.starts_with("sphere")) {
        addSphere(c.mesh, {0, c.h * 0.5f, 0}, {c.w * 0.5f, c.h * 0.5f, c.d * 0.5f},
                  id == "sphere1" ? std::max(4, c.detail / 2) : std::max(3, c.detail / 3),
                  id == "sphere1" ? c.detail * 2 : c.detail, "body");
    } else if (id == "spike") {
        addCylinder(c.mesh, {0, c.h * 0.5f, 0}, c.w * 0.5f, 0.0f, c.h, 4, "spike");
    } else if (id.starts_with("spikes")) {
        const int count = id == "spikes-big" ? 3 : 5;
        for (int z = 0; z < count; ++z)
            for (int x = 0; x < count; ++x) {
                const float px = -c.w * 0.5f + c.w * (float(x) + 0.5f) / float(count);
                const float pz = -c.d * 0.5f + c.d * (float(z) + 0.5f) / float(count);
                addCylinder(c.mesh, {px, c.h * 0.5f, pz}, std::min(c.w, c.d) * 0.35f / float(count), 0, c.h, 4,
                            "spikes");
            }
    } else if (id.starts_with("stairs")) {
        addStairs(c, id.starts_with("stairs-corner"));
    } else if (id == "toggle-switch") {
        addBox(c.mesh, {0, c.t * 0.5f, 0}, {c.w, c.t, c.d}, "base");
        addWedge(c.mesh, {0, c.t + (c.h - c.t) * 0.5f, 0}, {c.w * 0.3f, c.h - c.t, c.d * 0.3f}, false, "lever");
    } else if (id == "torus") {
        addTorus(c.mesh, {0, c.h * 0.5f, 0}, c.w * 0.36f, c.h * 0.5f, c.detail * 2, std::max(4, c.detail / 2), "ring");
    } else if (id.starts_with("wall-corner")) {
        addCorner(c, "wall");
    } else if (id.starts_with("wall-door")) {
        addFrame(c, c.w * 0.42f, c.h * 0.72f, 0.0f, "wall");
    } else if (id.starts_with("wall-window")) {
        addFrame(c, c.w * 0.5f, c.h * 0.42f, c.h * 0.28f, "wall");
    } else if (id.starts_with("wall")) {
        addBox(c.mesh, {0, c.h * 0.5f, 0}, {c.w, c.h, c.d}, "wall");
    } else if (id.starts_with("window")) {
        addFrame(c, c.w - 2.0f * c.t, c.h - 2.0f * c.t, c.t, "frame");
        addBox(c.mesh, {0, c.h * 0.5f, 0}, {c.t * 0.6f, c.h - 2.0f * c.t, c.d * 0.55f}, "mullion");
    }
}

RecipeDescriptor recipeDescriptor(const PrototypePieceDescriptor& piece) {
    RecipeDescriptor schema{"prototype." + std::string(piece.id), std::string(piece.displayName), "Prototype 3D", {}};
    schema.params.push_back(ParamDescriptor::floating("scale", "Uniform Scale", 1.0f, 0.01f, 100.0f, 0.05f));
    schema.params.push_back(ParamDescriptor::floating("width", "Width", piece.defaultWidth, 0.01f, 1000.0f, 0.05f));
    schema.params.push_back(ParamDescriptor::floating("height", "Height", piece.defaultHeight, 0.01f, 1000.0f, 0.05f));
    schema.params.push_back(ParamDescriptor::floating("depth", "Depth", piece.defaultDepth, 0.01f, 1000.0f, 0.05f));
    schema.params.push_back(ParamDescriptor::floating("thickness", "Member Thickness", 0.12f, 0.005f, 100.0f, 0.01f));
    schema.params.push_back(ParamDescriptor::integer("detail", "Radial Detail", 12, 3, 64));
    schema.params.push_back(ParamDescriptor::integer("steps", "Steps / Rungs", 6, 2, 64));
    schema.params.push_back(
        ParamDescriptor::floating("uvScale", "UV Tiles per World Unit", 0.5f, 0.01f, 100.0f, 0.05f));
    return schema;
}

}  // namespace

std::span<const PrototypePieceDescriptor> prototypePieceDescriptors() noexcept { return kPieces; }

eve::Result<MeshBuild> generatePrototypePiece(std::string_view pieceId, const Params& params) {
    const PrototypePieceDescriptor* piece = findPiece(pieceId);
    if (!piece) {
        return eve::Result<MeshBuild>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::NotFound, "unknown prototype piece", "pieceId"));
    }
    const float  scale   = positiveParam(params, "scale", 1.0f);
    const float  uvScale = positiveParam(params, "uvScale", 0.5f);
    BuildContext context{{},
                         positiveParam(params, "width", piece->defaultWidth) * scale,
                         positiveParam(params, "height", piece->defaultHeight) * scale,
                         positiveParam(params, "depth", piece->defaultDepth) * scale,
                         positiveParam(params, "thickness", 0.12f) * scale,
                         std::clamp(params.getInt("detail", 12), 3, 64),
                         std::clamp(params.getInt("steps", 6), 2, 64)};
    if (!std::isfinite(scale) || scale <= 0.0f || !std::isfinite(context.w) || !std::isfinite(context.h) ||
        !std::isfinite(uvScale) || !std::isfinite(context.d) || !std::isfinite(context.t) || context.w <= 0.0f ||
        context.h <= 0.0f || context.d <= 0.0f || context.t <= 0.0f) {
        return eve::Result<MeshBuild>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "prototype dimensions and scale must be finite and positive", "params"));
    }
    context.t = std::min({context.t, context.w * 0.45f, context.h * 0.45f, context.d * 0.45f});
    buildPiece(pieceId, context);
    if (context.mesh.empty()) {
        return eve::Result<MeshBuild>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "prototype generator produced an empty mesh", "pieceId"));
    }
    for (float& uv : context.mesh.uvs()) uv *= uvScale;
    context.mesh.setMeta("recipe", "prototype." + std::string(pieceId));
    context.mesh.setMeta("origin", "ground-center");
    context.mesh.setMeta("source", "procedural");
    return eve::Result<MeshBuild>::success(std::move(context.mesh), eve::Status::success(eve::StatusCode::Applied));
}

void registerPrototypePieceRecipes(MeshRecipeRegistry& registry) {
    for (const auto& piece : kPieces) {
        const std::string id(piece.id);
        registry.registerRecipe(recipeDescriptor(piece),
                                [id](const Params& params, MeshBuild& out, std::string& error) {
                                    auto generated = generatePrototypePiece(id, params);
                                    if (!generated.ok()) {
                                        error = generated.status().describe();
                                        return false;
                                    }
                                    out = std::move(generated).takeValue();
                                    return true;
                                });
    }
}

}  // namespace eve::procgen

// PrototypeKitPrimitives.cpp
namespace eve::procgen::prototype_detail {
namespace {

constexpr float kPi = 3.14159265358979323846f;

uint32_t addVertex(MeshBuild& mesh, Vec3 p, Vec3 n, float u, float v) {
    const auto index = static_cast<uint32_t>(mesh.getVertexCount());
    mesh.addVertex(p.x, p.y, p.z, n.x, n.y, n.z, u, v);
    return index;
}

struct UV {
    float u;
    float v;
};

UV projectUv(Vec3 p, Vec3 normal) {
    const float ax = std::abs(normal.x);
    const float ay = std::abs(normal.y);
    const float az = std::abs(normal.z);
    if (ay >= ax && ay >= az) return {p.x, p.z};
    if (ax >= az) return {p.z, p.y};
    return {p.x, p.y};
}

void addQuad(MeshBuild& mesh, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal) {
    const uint32_t base = static_cast<uint32_t>(mesh.getVertexCount());
    const UV       uvA  = projectUv(a, normal);
    const UV       uvB  = projectUv(b, normal);
    const UV       uvC  = projectUv(c, normal);
    const UV       uvD  = projectUv(d, normal);
    addVertex(mesh, a, normal, uvA.u, uvA.v);
    addVertex(mesh, b, normal, uvB.u, uvB.v);
    addVertex(mesh, c, normal, uvC.u, uvC.v);
    addVertex(mesh, d, normal, uvD.u, uvD.v);
    mesh.addTriangle(base, base + 1, base + 2);
    mesh.addTriangle(base, base + 2, base + 3);
}

Vec3 normalOf(Vec3 a, Vec3 b, Vec3 c) {
    const Vec3  u{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3  v{c.x - a.x, c.y - a.y, c.z - a.z};
    Vec3        n{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    const float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (length > 1.0e-6f) {
        n.x /= length;
        n.y /= length;
        n.z /= length;
    }
    return n;
}

}  // namespace

void addBox(MeshBuild& mesh, Vec3 center, Vec3 size, const char* group) {
    mesh.setActiveGroup(group);
    const float x0 = center.x - size.x * 0.5f;
    const float x1 = center.x + size.x * 0.5f;
    const float y0 = center.y - size.y * 0.5f;
    const float y1 = center.y + size.y * 0.5f;
    const float z0 = center.z - size.z * 0.5f;
    const float z1 = center.z + size.z * 0.5f;
    addQuad(mesh, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1});
    addQuad(mesh, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {0, 0, -1});
    addQuad(mesh, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {-1, 0, 0});
    addQuad(mesh, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {1, 0, 0});
    addQuad(mesh, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, {0, 1, 0});
    addQuad(mesh, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0});
}

void addWedge(MeshBuild& mesh, Vec3 center, Vec3 size, bool riseAlongX, const char* group) {
    mesh.setActiveGroup(group);
    const float x0 = center.x - size.x * 0.5f;
    const float x1 = center.x + size.x * 0.5f;
    const float y0 = center.y - size.y * 0.5f;
    const float y1 = center.y + size.y * 0.5f;
    const float z0 = center.z - size.z * 0.5f;
    const float z1 = center.z + size.z * 0.5f;
    Vec3        a{x0, y0, z0}, b{x1, y0, z0}, c{x1, y1, z0};
    Vec3        d{x0, y0, z1}, e{x1, y0, z1}, f{x1, y1, z1};
    if (!riseAlongX) {
        a = {x1, y0, z0};
        b = {x1, y0, z1};
        c = {x1, y1, z1};
        d = {x0, y0, z0};
        e = {x0, y0, z1};
        f = {x0, y1, z1};
    }
    const uint32_t first = static_cast<uint32_t>(mesh.getVertexCount());
    const Vec3     n0    = normalOf(a, c, b);
    const UV       uvA   = projectUv(a, n0);
    const UV       uvC   = projectUv(c, n0);
    const UV       uvB   = projectUv(b, n0);
    addVertex(mesh, a, n0, uvA.u, uvA.v);
    addVertex(mesh, c, n0, uvC.u, uvC.v);
    addVertex(mesh, b, n0, uvB.u, uvB.v);
    mesh.addTriangle(first, first + 1, first + 2);
    const uint32_t second = static_cast<uint32_t>(mesh.getVertexCount());
    const Vec3     n1     = normalOf(d, e, f);
    const UV       uvD    = projectUv(d, n1);
    const UV       uvE    = projectUv(e, n1);
    const UV       uvF    = projectUv(f, n1);
    addVertex(mesh, d, n1, uvD.u, uvD.v);
    addVertex(mesh, e, n1, uvE.u, uvE.v);
    addVertex(mesh, f, n1, uvF.u, uvF.v);
    mesh.addTriangle(second, second + 1, second + 2);
    addQuad(mesh, a, b, e, d, {0, -1, 0});
    const Vec3 slopeNormal = normalOf(a, d, f);
    addQuad(mesh, a, d, f, c, slopeNormal);
    const Vec3 highNormal = normalOf(b, c, f);
    addQuad(mesh, b, c, f, e, highNormal);
}

void addCylinder(MeshBuild& mesh, Vec3 center, float radiusBottom, float radiusTop, float height, int sides,
                 const char* group) {
    mesh.setActiveGroup(group);
    sides              = std::clamp(sides, 3, 64);
    const float y0     = center.y - height * 0.5f;
    const float y1     = center.y + height * 0.5f;
    const float radius = (radiusBottom + radiusTop) * 0.5f;
    for (int i = 0; i < sides; ++i) {
        const float    a0 = 2.0f * kPi * float(i) / float(sides);
        const float    a1 = 2.0f * kPi * float(i + 1) / float(sides);
        const Vec3     p0{center.x + std::cos(a0) * radiusBottom, y0, center.z + std::sin(a0) * radiusBottom};
        const Vec3     p1{center.x + std::cos(a1) * radiusBottom, y0, center.z + std::sin(a1) * radiusBottom};
        const Vec3     p2{center.x + std::cos(a1) * radiusTop, y1, center.z + std::sin(a1) * radiusTop};
        const Vec3     p3{center.x + std::cos(a0) * radiusTop, y1, center.z + std::sin(a0) * radiusTop};
        const Vec3     normal = normalOf(p0, p2, p1);
        const uint32_t side   = static_cast<uint32_t>(mesh.getVertexCount());
        addVertex(mesh, p0, normal, a0 * radius, y0);
        addVertex(mesh, p1, normal, a1 * radius, y0);
        addVertex(mesh, p2, normal, a1 * radius, y1);
        addVertex(mesh, p3, normal, a0 * radius, y1);
        mesh.addTriangle(side, side + 2, side + 1);
        mesh.addTriangle(side, side + 3, side + 2);
        const uint32_t bottom = static_cast<uint32_t>(mesh.getVertexCount());
        addVertex(mesh, {center.x, y0, center.z}, {0, -1, 0}, center.x, center.z);
        addVertex(mesh, p1, {0, -1, 0}, p1.x, p1.z);
        addVertex(mesh, p0, {0, -1, 0}, p0.x, p0.z);
        mesh.addTriangle(bottom, bottom + 2, bottom + 1);
        const uint32_t top = static_cast<uint32_t>(mesh.getVertexCount());
        addVertex(mesh, {center.x, y1, center.z}, {0, 1, 0}, center.x, center.z);
        addVertex(mesh, p3, {0, 1, 0}, p3.x, p3.z);
        addVertex(mesh, p2, {0, 1, 0}, p2.x, p2.z);
        mesh.addTriangle(top, top + 2, top + 1);
    }
}

void addSphere(MeshBuild& mesh, Vec3 center, Vec3 radii, int rings, int sides, const char* group) {
    mesh.setActiveGroup(group);
    rings               = std::clamp(rings, 2, 32);
    sides               = std::clamp(sides, 3, 64);
    const uint32_t base = static_cast<uint32_t>(mesh.getVertexCount());
    for (int ring = 0; ring <= rings; ++ring) {
        const float v   = float(ring) / float(rings);
        const float phi = v * kPi;
        for (int side = 0; side <= sides; ++side) {
            const float u     = float(side) / float(sides);
            const float theta = u * 2.0f * kPi;
            const Vec3  n{std::sin(phi) * std::cos(theta), std::cos(phi), std::sin(phi) * std::sin(theta)};
            const float horizontalRadius = (radii.x + radii.z) * 0.5f;
            addVertex(mesh, {center.x + n.x * radii.x, center.y + n.y * radii.y, center.z + n.z * radii.z}, n,
                      theta * horizontalRadius, phi * radii.y);
        }
    }
    for (int ring = 0; ring < rings; ++ring) {
        for (int side = 0; side < sides; ++side) {
            const uint32_t a = base + uint32_t(ring * (sides + 1) + side);
            const uint32_t b = a + uint32_t(sides + 1);
            mesh.addTriangle(a, a + 1, b);
            mesh.addTriangle(a + 1, b + 1, b);
        }
    }
}

void addTorus(MeshBuild& mesh, Vec3 center, float majorRadius, float minorRadius, int majorSegments, int minorSegments,
              const char* group) {
    mesh.setActiveGroup(group);
    majorSegments       = std::clamp(majorSegments, 3, 64);
    minorSegments       = std::clamp(minorSegments, 3, 32);
    const uint32_t base = static_cast<uint32_t>(mesh.getVertexCount());
    for (int major = 0; major <= majorSegments; ++major) {
        const float u = float(major) / float(majorSegments);
        const float a = 2.0f * kPi * u;
        for (int minor = 0; minor <= minorSegments; ++minor) {
            const float v = float(minor) / float(minorSegments);
            const float b = 2.0f * kPi * v;
            const Vec3  n{std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b)};
            const float radial = majorRadius + minorRadius * std::cos(b);
            addVertex(mesh,
                      {center.x + radial * std::cos(a), center.y + minorRadius * std::sin(b),
                       center.z + radial * std::sin(a)},
                      n, a * majorRadius, b * minorRadius);
        }
    }
    for (int major = 0; major < majorSegments; ++major) {
        for (int minor = 0; minor < minorSegments; ++minor) {
            const uint32_t a = base + uint32_t(major * (minorSegments + 1) + minor);
            const uint32_t b = a + uint32_t(minorSegments + 1);
            mesh.addTriangle(a, a + 1, b);
            mesh.addTriangle(a + 1, b + 1, b);
        }
    }
}

}  // namespace eve::procgen::prototype_detail
