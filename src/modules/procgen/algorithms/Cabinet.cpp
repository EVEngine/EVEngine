#include "procgen/algorithms/Cabinet.h"

#include "procgen/algorithms/MarchingCubes.h"
#include "procgen/algorithms/PrototypeKitPrimitives.h"

#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace eve::procgen {
eve::Result<MeshBuild> generateCabinet(const Params& p) {
    const float invalid = std::numeric_limits<float>::quiet_NaN();
    auto number   = [&](const char* key, float fallback) { return p.getFloat(key, p.has(key) ? invalid : fallback); };
    const float w = number("cabinetWidth", 1.8f), h = number("cabinetHeight", 2.0f), d = number("depth", 0.55f);
    const float t = number("thickness", 0.025f), leg = p.getBool("legs", true) ? number("legHeight", 0.15f) : 0.f;
    const int   columns     = p.getInt("columns", p.has("columns") ? -1 : 2),
              levels        = p.getInt("levels", p.has("levels") ? -1 : 3);
    const int doors         = p.getInt("doors", p.has("doors") ? -1 : 2),
              drawers       = p.getInt("drawers", p.has("drawers") ? -1 : 2);
    const std::string style = p.getString("style", "modern");
    auto              fail  = [] {
        return eve::Result<MeshBuild>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                 "invalid cabinet dimensions, counts, style or insufficient clearance", "cabinet"));
    };
    if (!std::isfinite(w) || !std::isfinite(h) || !std::isfinite(d) || !std::isfinite(t) || !std::isfinite(leg) ||
        w > 100 || h > 100 || d > 100 || t < 0.002f || leg < 0 || columns < 1 || columns > 16 || levels < 1 ||
        levels > 16 || doors < 0 || doors > 16 || drawers < 0 || drawers > 16 ||
        (style != "modern" && style != "minimal" && style != "european" && style != "chinese"))
        return fail();
    const bool  hasCornice = style == "european" || style == "chinese";
    const float bodyTop    = hasCornice ? h - 2 * t : h;
    const float inner = w - 2 * t, bottom = leg + t, top = bodyTop - t;
    const float drawerH = drawers ? (top - bottom) * 0.35f / drawers : 0;
    const float split   = bottom + drawerH * drawers;
    const float bay     = (inner - (columns - 1) * t) / columns;
    const float doorW   = doors ? inner / doors : inner;
    if (d < 8 * t || bay < 6 * t || top - bottom < 12 * t || (top - split) / levels < 4 * t ||
        (drawers && drawerH < 4 * t) || (doors && doorW < 6 * t))
        return fail();
    MeshBuild m;
    auto      box = [&](float x, float y, float z, float sx, float sy, float sz, const std::string& group) {
        const int firstVertex = m.getVertexCount();
        prototype_detail::addBox(m, {x, y, z}, {sx, sy, sz}, group.c_str());
        // Drawer fronts and crown moulding have horizontal grain; doors remain vertical.
        if ((group.starts_with("drawer.") && !group.ends_with(".interior") && !group.ends_with(".hardware")) ||
            group == "cornice") {
            for (int i = firstVertex; i < m.getVertexCount(); ++i)
                std::swap(m.uvs()[size_t(i) * 2], m.uvs()[size_t(i) * 2 + 1]);
        }
    };
    box(0, leg + t / 2, 0, w, t, d, "body");
    box(0, bodyTop - t / 2, 0, w, t, d, "body");
    for (float sign : {-1.f, 1.f}) box(sign * (w - t) / 2, (leg + bodyTop) / 2, 0, t, bodyTop - leg, d, "body");
    box(0, (leg + bodyTop) / 2, -(d - t) / 2, inner, bodyTop - leg - 2 * t, t, "back");
    for (int c = 1; c < columns; ++c)
        box(-inner / 2 + c * (bay + t) - t / 2, (split + top) / 2, 0, t, top - split, d - 2 * t, "dividers");
    for (int l = 1; l < levels; ++l) box(0, split + (top - split) * l / levels, 0, inner, t, d - 2 * t, "shelves");
    if (drawers) box(0, split, 0, inner, t, d - 2 * t, "shelves");
    if (leg > 0)
        for (float x : {-1.f, 1.f})
            for (float z : {-1.f, 1.f}) {
                if (style == "european")
                    prototype_detail::addCylinder(m, {x * (w / 2 - 2 * t), leg / 2, z * (d / 2 - 2 * t)}, t, 1.5f * t,
                                                  leg, 12, "legs");
                else
                    box(x * (w / 2 - 2 * t), leg / 2, z * (d / 2 - 2 * t), 2 * t, leg, 2 * t, "legs");
            }
    auto front = [&](float x, float y, float fw, float fh, const std::string& group, bool drawer, float handleSide) {
        const float z = d / 2 - t / 2;
        box(x, y, z, fw, fh, t, group);
        if (style == "european" || style == "chinese") {
            for (float sign : {-1.f, 1.f}) {
                box(x + sign * (fw / 2 - t), y, z + t, t, fh - t, t, group + ".trim");
                box(x, y + sign * (fh / 2 - t), z + t, fw - t, t, t, group + ".trim");
            }
            if (style == "chinese")
                for (int i = -1; i <= 1; ++i) box(x + i * fw / 5, y, z + t, 0.4f * t, fh - 4 * t, t, group + ".trim");
        }
        if (style != "minimal") {
            const float hx = drawer ? x : x + handleSide * (fw / 2 - 2 * t);
            box(hx, y, z + 2 * t, drawer ? fw * 0.3f : t, drawer ? t : fh * 0.15f, t, group + ".hardware");
        }
    };
    for (int i = 0; i < doors; ++i)
        front(-inner / 2 + (i + 0.5f) * doorW, (split + top) / 2, doorW - t * 0.25f, top - split - t * 0.5f,
              "door." + std::to_string(i), false, i % 2 == 0 ? 1.f : -1.f);
    for (int i = 0; i < drawers; ++i) {
        const std::string group = "drawer." + std::to_string(i);
        const float       y     = bottom + (i + 0.5f) * drawerH;
        front(0, y, inner - t * 0.5f, drawerH - t * 0.5f, group, true, 1.f);
        box(0, y - drawerH / 2 + t, 0, inner - 2 * t, t, d - 4 * t, group + ".interior");
        for (float s : {-1.f, 1.f}) box(s * (inner / 2 - t), y, -t, t, drawerH - 2 * t, d - 4 * t, group + ".interior");
        box(0, y, -d / 2 + 2 * t, inner - 2 * t, drawerH - 2 * t, t, group + ".interior");
    }
    if (hasCornice) {
        box(0, h - 1.5f * t, 0, w + 2 * t, t, d + 2 * t, "cornice");
        box(0, h - t / 2, 0, w + 4 * t, t, d + 4 * t, "cornice");
    }
    for (const auto& group : m.groupNames()) {
        std::string role = "exterior";
        if (group.ends_with(".hardware"))
            role = "hardware";
        else if (group.ends_with(".trim") || group == "cornice")
            role = "trim";
        else if (group.ends_with(".interior") || group == "back" || group == "shelves" || group == "dividers")
            role = "interior";
        else if (group == "legs")
            role = "legs";
        m.setMeta("material." + group, role);
    }
    m.setMeta("recipe", "mesh.cabinet");
    m.setMeta("style", style);
    m.setMeta("origin", "ground-center");
    return eve::Result<MeshBuild>::success(std::move(m));
}

void registerCabinetRecipe(MeshRecipeRegistry& registry) {
    RecipeDescriptor s;
    s.id          = "mesh.cabinet";
    s.displayName = "Cabinet";
    s.category    = "Furniture";
    s.params      = {ParamDescriptor::floating("cabinetWidth", "Width", 1.8f, 0.1f, 100, 0.05f),
                     ParamDescriptor::floating("cabinetHeight", "Height", 2, 0.1f, 100, 0.05f),
                     ParamDescriptor::floating("depth", "Depth", 0.55f, 0.1f, 100, 0.05f),
                     ParamDescriptor::floating("thickness", "Panel thickness", 0.025f, 0.002f, 1, 0.005f),
                     ParamDescriptor::integer("columns", "Compartments", 2, 1, 16),
                     ParamDescriptor::integer("levels", "Storage levels", 3, 1, 16),
                     ParamDescriptor::integer("doors", "Door leaves", 2, 0, 16),
                     ParamDescriptor::integer("drawers", "Drawers", 2, 0, 16),
                     ParamDescriptor::boolean("legs", "Raised legs", true),
                     ParamDescriptor::floating("legHeight", "Leg height", 0.15f, 0, 2, 0.01f),
                     ParamDescriptor::choice("style", "Style", "modern", {"european", "chinese", "minimal", "modern"})};
    registry.registerRecipe(std::move(s), [](const Params& p, MeshBuild& out, std::string& error) {
        auto result = generateCabinet(p);
        if (!result.ok()) {
            error = result.status().describe();
            return false;
        }
        out = std::move(result).takeValue();
        return true;
    });
}
}  // namespace eve::procgen
