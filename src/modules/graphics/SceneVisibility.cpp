#include "graphics/SceneVisibility.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <map>
#include <optional>
#include <set>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SceneLocalRegion.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"
#include "graphics/ClipSpace.h"
#include "graphics/Graphics.h"
#include "graphics/GraphicsVisibility.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"

namespace eve::graphics {
namespace {
const Value* field(const Value& v, const char* key) {
    const auto* o = v.getIf<Value::Object>();
    if (!o) return nullptr;
    auto it = o->find(key);
    return it == o->end() ? nullptr : &it->second;
}
const std::string* text(const Value& v, const char* key) {
    const auto* item = field(v, key);
    return item ? item->getIf<std::string>() : nullptr;
}
bool number(const Value* v, double& out) {
    if (!v) return false;
    if (auto p = v->getIf<double>())
        out = *p;
    else if (auto p = v->getIf<std::int64_t>())
        out = double(*p);
    else
        return false;
    return std::isfinite(out);
}
bool array(const Value* v, size_t count) {
    const auto* a = v ? v->getIf<Value::Array>() : nullptr;
    if (!a || a->size() != count) return false;
    for (const auto& item : *a) {
        double n = 0;
        if (!number(&item, n) || std::abs(n) > 1e8) return false;
    }
    return true;
}
glm::vec3 vec(const Value& v) {
    glm::vec3   out;
    const auto& a = *v.getIf<Value::Array>();
    for (int i = 0; i < 3; ++i) {
        double n = 0;
        number(&a[i], n);
        out[i] = float(n);
    }
    return out;
}
Result<Value> fail(DiagnosticCode code, const char* message) {
    return Result<Value>::failure(Diagnostic::error(code, message));
}
bool validPixels(const VisibilityPixels& pixels, int width, int height) {
    const auto count = size_t(width) * size_t(height);
    return pixels.width == width && pixels.height == height && pixels.ids.size() == count &&
           pixels.depth.size() == count && std::all_of(pixels.depth.begin(), pixels.depth.end(), [](float depth) {
               return std::isfinite(depth) && depth >= 0 && depth <= 1;
           });
}
bool fields(const Value& v, const std::set<std::string>& allowed) {
    const auto* o = v.getIf<Value::Object>();
    if (!o) return false;
    for (const auto& [k, item] : *o) {
        (void)item;
        if (!allowed.contains(k)) return false;
    }
    return true;
}
}  // namespace
Result<SceneVisibilityQuery> SceneVisibilityQuery::prepare(const Value& request) {
    const auto reject = [] {
        return Result<SceneVisibilityQuery>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument,
            "Invalid eve.scene.visibility-request v1/v2; bounded viewport, camera, draws and targets required"));
    };
    if (!fields(request, {"schemaId", "schemaVersion", "sourceRevision", "candidateGeneration", "camera", "width",
                          "height", "draws", "targets"}))
        return reject();
    auto   schema  = text(request, "schemaId");
    double version = 0, width = 0, height = 0, revision = 0, generation = 0;
    if (!schema || *schema != "eve.scene.visibility-request" || !number(field(request, "schemaVersion"), version) ||
        (version != 1 && version != 2) || !number(field(request, "width"), width) ||
        !number(field(request, "height"), height) || width < 1 || height < 1 || width > 2048 || height > 2048 ||
        std::floor(width) != width || std::floor(height) != height || width * height > 1048576 ||
        !number(field(request, "sourceRevision"), revision) || revision < 0 || std::floor(revision) != revision ||
        !number(field(request, "candidateGeneration"), generation) || generation < 0 ||
        std::floor(generation) != generation)
        return reject();
    const auto* camera = field(request, "camera");
    double      fov = 0, nearZ = 0, farZ = 0;
    if (!camera || !fields(*camera, {"eye", "target", "up", "fov", "near", "far"}) ||
        !array(field(*camera, "eye"), 3) || !array(field(*camera, "target"), 3) || !array(field(*camera, "up"), 3) ||
        !number(field(*camera, "fov"), fov) || fov <= 1 || fov >= 179 || !number(field(*camera, "near"), nearZ) ||
        nearZ < 1e-6 || !number(field(*camera, "far"), farZ) || farZ <= nearZ || farZ > 1e8)
        return reject();
    auto forward = vec(*field(*camera, "target")) - vec(*field(*camera, "eye"));
    if (glm::length(forward) < 1e-6f || glm::length(glm::cross(forward, vec(*field(*camera, "up")))) < 1e-6f)
        return reject();
    const auto* drawsValue   = field(request, "draws");
    const auto* draws        = drawsValue ? drawsValue->getIf<Value::Array>() : nullptr;
    const auto* targetsValue = field(request, "targets");
    const auto* targets      = targetsValue ? targetsValue->getIf<Value::Array>() : nullptr;
    if (!draws || draws->empty() || draws->size() > 4096 || !targets || targets->empty() || targets->size() > 16)
        return reject();
    std::set<std::string> objects, identities;
    for (const auto& draw : *draws) {
        const auto* object = text(draw, "object");
        double      id = 0, gen = 0;
        if (!fields(draw, {"object", "entityId", "generation", "model"}) || !object || object->empty() ||
            !number(field(draw, "entityId"), id) || id < 0 || id > UINT32_MAX || std::floor(id) != id ||
            !number(field(draw, "generation"), gen) || gen < 0 || gen > UINT32_MAX || std::floor(gen) != gen ||
            !array(field(draw, "model"), 16))
            return reject();
        if (!identities.insert(std::to_string(id) + ":" + std::to_string(gen)).second) return reject();
        const auto& model = *field(draw, "model")->getIf<Value::Array>();
        double      n     = 0;
        for (int i : {3, 7, 11, 15}) {
            number(&model[i], n);
            if (n != (i == 15 ? 1 : 0)) return reject();
        }
        objects.insert(*object);
    }
    std::set<std::pair<std::string, std::string>> unique;
    for (const auto& target : *targets) {
        const auto* id = target.getIf<std::string>();
        std::string regionId;
        if (!id) {
            if (version != 2 || !fields(target, {"object", "region"})) return reject();
            id                = text(target, "object");
            const auto* value = field(target, "region");
            if (!value) return reject();
            auto region = SceneLocalRegion::fromValue(*value);
            if (!region.ok()) return Result<SceneVisibilityQuery>::failure(region.status());
            regionId = region.value().id;
        }
        if (!id || !objects.contains(*id) || !unique.emplace(*id, regionId).second) return reject();
    }
    if (width * height * (targets->size() + 1) > 4194304) return reject();
    SceneVisibilityQuery query;
    query.request_ = request;
    return Result<SceneVisibilityQuery>::success(std::move(query));
}
Result<Value> SceneVisibilityQuery::measure(Graphics& graphics) {
    if (!field(request_, "camera"))
        return fail(DiagnosticCode::InvalidArgument, "Visibility query is empty or moved from");
    if (consumed_) return fail(DiagnosticCode::Conflict, "Visibility query has already been consumed");
    consumed_ = true;
    double n  = 0;
    number(field(request_, "width"), n);
    int width = int(n);
    number(field(request_, "height"), n);
    int         height = int(n);
    const auto& camera = *field(request_, "camera");
    double      fov = 0, nearZ = 0, farZ = 0;
    number(field(camera, "fov"), fov);
    number(field(camera, "near"), nearZ);
    number(field(camera, "far"), farZ);
    const auto view =
        glm::lookAtRH(vec(*field(camera, "eye")), vec(*field(camera, "target")), vec(*field(camera, "up")));
    const auto projection =
        perspectiveVulkanRH_ZO(float(fov * 3.141592653589793 / 180), float(width) / height, float(nearZ), float(farZ));
    std::vector<EntityIdDraw>            draws;
    std::map<std::string, std::uint32_t> mapping;
    for (const auto& input : *field(request_, "draws")->getIf<Value::Array>()) {
        number(field(input, "entityId"), n);
        auto id = std::uint32_t(n);
        number(field(input, "generation"), n);
        auto              generation = std::uint32_t(n);
        ecs::EntityHandle handle{ecs::current(), std::type_index(typeid(Renderable3D)), id, generation};
        auto*             entity = dynamic_cast<Renderable3D*>(ecs::try_get(handle));
        if (!entity) return fail(DiagnosticCode::StaleHandle, "Visibility renderable generation is stale");
        const auto  renderer = entity->meshRenderer();
        const auto* object   = text(input, "object");
        // Reject any appearance/deformation path this opaque pass cannot reproduce.
        if (!renderer->mesh || !renderer->mesh->gpuHandle)
            return fail(DiagnosticCode::StaleHandle, "Visibility mesh is unavailable");
        if (renderer->material || renderer->texture || renderer->shader || renderer->heightTexture ||
            renderer->isHair || renderer->a != 1.f || renderer->partCount || renderer->lodCount ||
            renderer->instances || renderer->xrayHighlight || renderer->mesh->hasGpuSkinning() ||
            renderer->mesh->isMorphDirty())
            return fail(DiagnosticCode::Unsupported,
                        "Visibility currently requires untextured opaque static single-part meshes");
        auto [it, inserted] = mapping.emplace(*object, std::uint32_t(mapping.size() + 1));
        (void)inserted;
        EntityIdDraw draw;
        draw.mesh          = renderer->mesh;
        const auto& values = *field(input, "model")->getIf<Value::Array>();
        for (int i = 0; i < 16; ++i) {
            number(&values[i], n);
            draw.model[i / 4][i % 4] = float(n);
        }
        if (std::abs(glm::determinant(draw.model)) < 1e-10f)
            return fail(DiagnosticCode::InvalidArgument, "Visibility model is singular");
        auto packed = it->second;
        draw.idColor =
            glm::vec4((packed & 255) / 255.f, ((packed >> 8) & 255) / 255.f, ((packed >> 16) & 255) / 255.f, 1.f);
        draws.push_back(draw);
    }
    auto* capture = graphics.visibilityCapture();
    if (!capture) return fail(DiagnosticCode::Unsupported, "Backend has no isolated visibility capture");
    auto scene = capture->captureVisibilityPixels(draws, projection * view, width, height);
    if (!scene.ok()) return Result<Value>::failure(scene.status());
    if (!validPixels(scene.value(), width, height))
        return fail(DiagnosticCode::InvariantViolation, "Invalid scene raster evidence");
    Value::Array  reports;
    Value::Object objectMap;
    for (const auto& [object, id] : mapping) objectMap[object] = int(id);
    constexpr float                                   depthEpsilon = 1e-6f;
    std::map<std::string, VisibilityPixels>           fullCache;
    std::vector<std::vector<std::uint8_t>>            targetMasks;
    for (const auto& target : *field(request_, "targets")->getIf<Value::Array>()) {
        const auto& name = target.getIf<std::string>() ? *target.getIf<std::string>() : *text(target, "object");
        auto        id   = mapping.at(name);
        std::vector<EntityIdDraw> isolated;
        for (const auto& draw : draws)
            if (std::uint32_t(std::lround(draw.idColor.r * 255)) +
                    (std::uint32_t(std::lround(draw.idColor.g * 255)) << 8) +
                    (std::uint32_t(std::lround(draw.idColor.b * 255)) << 16) ==
                id)
                isolated.push_back(draw);
        if (!fullCache.contains(name)) {
            auto full = capture->captureVisibilityPixels(isolated, projection * view, width, height);
            if (!full.ok()) return Result<Value>::failure(full.status());
            if (!validPixels(full.value(), width, height))
                return fail(DiagnosticCode::InvariantViolation, "Invalid target raster evidence");
            fullCache.emplace(name, std::move(full).takeValue());
        }
        const auto&                     full = fullCache.at(name);
        std::optional<SceneLocalRegion> region;
        glm::dmat4                      inverseClip(1.0);
        if (!target.getIf<std::string>()) {
            auto decoded = SceneLocalRegion::fromValue(*field(target, "region"));
            if (!decoded.ok()) return Result<Value>::failure(decoded.status());
            region = std::move(decoded).takeValue();
            for (const auto& draw : isolated)
                if (draw.model != isolated.front().model)
                    return fail(DiagnosticCode::Unsupported,
                                "Region requires one object-local frame for all target meshes");
            inverseClip = glm::inverse(glm::dmat4(projection * view * isolated.front().model));
        }
        std::vector<std::uint8_t>             mask(full.ids.size(), 0);
        std::int64_t                          total = 0, visible = 0, ambiguous = 0;
        std::map<std::uint32_t, std::int64_t> occluders;
        for (size_t p = 0; p < full.ids.size(); ++p) {
            if (full.ids[p] != id) continue;
            if (region) {
                const auto x = p % size_t(width), y = p / size_t(width);
                glm::dvec4 local = inverseClip * glm::dvec4(2.0 * (double(x) + 0.5) / width - 1.0,
                                                            2.0 * (double(y) + 0.5) / height - 1.0, full.depth[p], 1.0);
                if (!std::isfinite(local.w) || std::abs(local.w) < 1e-12)
                    return fail(DiagnosticCode::InvariantViolation, "Region surface unprojection is singular");
                local /= local.w;
                bool inside = true;
                for (int axis = 0; axis < 3; ++axis) {
                    if (!std::isfinite(local[axis]))
                        return fail(DiagnosticCode::InvariantViolation, "Region surface unprojection is nonfinite");
                    const double epsilon =
                        1e-4 * std::max({1.0, std::abs(region->minimum[axis]), std::abs(region->maximum[axis])});
                    inside = inside && local[axis] >= region->minimum[axis] - epsilon &&
                             local[axis] <= region->maximum[axis] + epsilon;
                }
                if (!inside) continue;
            }
            mask[p] = 1;
            ++total;
            auto front = scene.value().ids[p];
            if (front == id)
                ++visible;
            else if (front && scene.value().depth[p] + depthEpsilon < full.depth[p])
                ++occluders[front];
            else
                ++ambiguous;
        }
        Value::Array hiddenBy;
        for (const auto& [object, other] : mapping)
            if (occluders.contains(other))
                hiddenBy.emplace_back(Value::Object{{"object", object},
                                                    {"pixels", occluders.at(other)},
                                                    {"fraction", total ? double(occluders.at(other)) / total : 0}});
        Value::Object report{{"object", name},
                             {"status", total < 4   ? "Unmeasurable"
                                        : ambiguous ? "AmbiguousDepth"
                                                    : "Measured"},
                             {"fullPixels", total},
                             {"visiblePixels", visible},
                             {"visibleFraction", total ? double(visible) / total : 0},
                             {"ambiguousPixels", ambiguous},
                             {"occluders", std::move(hiddenBy)}};
        if (region) {
            report["region"]                        = region->toValue();
            report["regionBoundaryEpsilonRelative"] = 1e-4;
        }
        reports.emplace_back(std::move(report));
        targetMasks.push_back(std::move(mask));
    }
    // Depth-independent intersection of real isolated silhouettes, never bounding rectangles.
    // Only whole-object partners are emitted; a target may itself be a local region.
    for (size_t index = 0; index < reports.size(); ++index) {
        auto&        report = *reports[index].getIf<Value::Object>();
        const auto   total  = *report.at("fullPixels").getIf<std::int64_t>();
        Value::Array overlaps;
        for (size_t other = 0; other < reports.size(); ++other) {
            const auto& partner = *reports[other].getIf<Value::Object>();
            if (partner.contains("region") || partner.at("object") == report.at("object")) continue;
            const auto   partnerTotal = *partner.at("fullPixels").getIf<std::int64_t>();
            std::int64_t intersection = 0;
            for (size_t pixel = 0; pixel < targetMasks[index].size(); ++pixel)
                if (targetMasks[index][pixel] && targetMasks[other][pixel]) ++intersection;
            overlaps.emplace_back(
                Value::Object{{"object", partner.at("object")},
                              {"status", total >= 4 && partnerTotal >= 4 ? "Measured" : "Unmeasurable"},
                              {"intersectionPixels", intersection},
                              {"referencePixels", total},
                              {"subjectPixels", partnerTotal},
                              {"fraction", total ? double(intersection) / total : 0.0}});
        }
        report["projectedOverlaps"] = std::move(overlaps);
    }
    static std::int64_t captureGeneration = 0;
    return Result<Value>::success(Value::Object{{"schemaId", "eve.scene.visibility-evidence"},
                                                {"schemaVersion", 3},
                                                {"captureId", ++captureGeneration},
                                                {"sourceRevision", *field(request_, "sourceRevision")},
                                                {"candidateGeneration", *field(request_, "candidateGeneration")},
                                                {"camera", camera},
                                                {"width", width},
                                                {"height", height},
                                                {"objectIds", std::move(objectMap)},
                                                {"targets", std::move(reports)},
                                                {"depthEpsilonNdc", double(depthEpsilon)},
                                                {"precision", "opaque-triangle-raster"},
                                                {"readbackComplete", true},
                                                {"scope", "explicit-authoring-draw-set"},
                                                {"request", request_}});
}
void exposeSceneVisibility(ssq::Table& table) {
    const auto vm  = table.getHandle();
    auto       cls = table.addClass<SceneVisibilityQuery>(
        "SceneVisibilityQuery",
        std::function<SceneVisibilityQuery*()>([]() -> SceneVisibilityQuery* { return nullptr; }), true);
    cls.addFunc("measure", [vm](SceneVisibilityQuery* query, Graphics* graphics) {
        if (!graphics)
            return script::projectStatusResult(
                vm, Status::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "Graphics required")));
        auto result = query->measure(*graphics);
        if (!result.ok()) return script::projectStatusResult(vm, result.status());
        return script::projectStatusResult(vm, result.status(), std::move(result).takeValue());
    });
    table.addFunc("prepareSceneVisibility", [vm](ssq::Object request) {
        auto value = script::valueFromSquirrel(request);
        if (!value.ok()) return script::projectStatusResult(vm, value.status());
        auto prepared = SceneVisibilityQuery::prepare(value.value());
        if (!prepared.ok()) return script::projectStatusResult(vm, prepared.status());
        auto owned = script::makeOwnedSquirrelInstance<SceneVisibilityQuery>(
            vm, std::make_unique<SceneVisibilityQuery>(std::move(prepared).takeValue()));
        if (!owned.ok()) return script::projectStatusResult(vm, owned.status());
        return script::projectStatusResult(vm, owned.status(), std::move(owned).takeValue());
    });
}
}  // namespace eve::graphics
