#include "asset/graphics/EvpackStaticPrefab.h"
#include "asset/RuntimeDefinition.h"
#include "asset/graphics/EvpackGraphicsLoader.h"
#include "asset/graphics/EvpackImageLoader.h"
#include "asset/scene/EvpackSceneTemplateLoader.h"
#include "graphics/Graphics.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>

namespace eve::asset_graphics {
namespace {
template <class T>
Result<T> fail(std::string message) {
    return Result<T>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), {}, {}, "asset.graphics.prefab"));
}
struct MaterialData {
    graphics::Color         color{1.f, 1.f, 1.f, 1.f};
    float                   metallic = 0.f, roughness = 1.f;
    std::optional<AssetRef> image;
};
Result<MaterialData> material(const asset::EvpackResourceReader& reader, const AssetRef& ref,
                              const asset::EvpackCapabilities& caps) {
    auto payload = reader.read(ref, "eve.material/1", caps, 1024 * 1024);
    if (!payload) return Result<MaterialData>::failure(payload.status());
    if (payload.value().chunks.size() != 1) return fail<MaterialData>("material requires exactly one definition");
    auto decoded = asset::decodeRuntimeDefinition(payload.value().chunks.front().bytes);
    if (!decoded) return Result<MaterialData>::failure(decoded.status());
    const auto* object = decoded.value().getIf<Value::Object>();
    if (!object) return fail<MaterialData>("material definition must be an object");
    auto field = [&](const char* key) -> const Value* {
        const auto found = object->find(key);
        return found == object->end() ? nullptr : &found->second;
    };
    auto equal = [&](const char* key, const char* expected) {
        const auto* value = field(key);
        return value && value->isString() && value->asString() == expected;
    };
    const auto* version = field("schemaVersion");
    if (!equal("schema", "eve.material") || !version || !version->isInt64() || version->asInt() != 1 ||
        !equal("shadingModel", "pbr") || !equal("surfaceMode", "opaque"))
        return fail<MaterialData>("unsupported material definition");
    auto scalar = [](const Value* value, float& output) {
        if (!value || !value->isNumeric()) return false;
        double number = value->isInt64() ? double(value->asInt()) : value->asDouble();
        if (!std::isfinite(number) || number < 0 || number > 1) return false;
        output = float(number);
        return true;
    };
    MaterialData out;
    const auto*  color    = field("baseColor");
    const auto*  channels = color ? color->getIf<Value::Array>() : nullptr;
    if (!channels || channels->size() != 4 || !scalar(&(*channels)[0], out.color.r) ||
        !scalar(&(*channels)[1], out.color.g) || !scalar(&(*channels)[2], out.color.b) ||
        !scalar(&(*channels)[3], out.color.a) || !scalar(field("metallic"), out.metallic) ||
        !scalar(field("roughness"), out.roughness))
        return fail<MaterialData>("invalid material factors");
    if (const auto* image = field("baseColorTexture")) {
        if (!image->isString()) return fail<MaterialData>("invalid material image reference");
        auto parsed = AssetRef::parse(image->asString());
        if (!parsed) return Result<MaterialData>::failure(parsed.status());
        out.image = std::move(parsed).takeValue();
    }
    return Result<MaterialData>::success(std::move(out));
}
}  // namespace

struct EvpackStaticPrefab::Impl {
    graphics::IMeshResourceFactory&  meshes;
    graphics::IImageResourceFactory& images;
    // Factory ABI leases: only passed back to the same factories or synchronous draw calls.
    std::map<std::string, graphics::Mesh*>    meshLeases;
    std::map<std::string, graphics::Texture*> imageLeases;
    struct Draw {
        glm::mat4          transform;
        graphics::Mesh*    mesh;
        graphics::Texture* image;
        MaterialData       material;
    };
    std::vector<Draw> draws;
    bool              released = false;
};
EvpackStaticPrefab::EvpackStaticPrefab(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
EvpackStaticPrefab::~EvpackStaticPrefab() {
    auto result = release();
    if (!result) std::fprintf(stderr, "asset prefab release failed: %s\n", result.error()->message().c_str());
}
std::size_t  EvpackStaticPrefab::drawCount() const noexcept { return impl_->draws.size(); }
Result<void> EvpackStaticPrefab::release() {
    impl_->released = true;
    impl_->draws.clear();
    Status failure;
    for (auto it = impl_->meshLeases.begin(); it != impl_->meshLeases.end();) {
        auto result = impl_->meshes.releaseMesh(it->second);
        if (result)
            it = impl_->meshLeases.erase(it);
        else {
            failure = result.status();
            ++it;
        }
    }
    for (auto it = impl_->imageLeases.begin(); it != impl_->imageLeases.end();) {
        auto result = impl_->images.releaseImage(it->second);
        if (result)
            it = impl_->imageLeases.erase(it);
        else {
            failure = result.status();
            ++it;
        }
    }
    if (failure.isFailure()) return Result<void>::failure(failure);
    return Result<void>::success();
}
Result<std::unique_ptr<EvpackStaticPrefab>> EvpackStaticPrefab::load(const asset::EvpackResourceReader& reader,
                                                                     graphics::IMeshResourceFactory&    meshes,
                                                                     graphics::IImageResourceFactory&   images,
                                                                     const AssetRef&                    ref,
                                                                     const asset::EvpackCapabilities&   caps) {
    auto scene = asset_scene::EvpackSceneTemplateLoader(reader).load(ref, caps);
    if (!scene) return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(scene.status());
    auto out =
        std::unique_ptr<EvpackStaticPrefab>(new EvpackStaticPrefab(std::make_unique<Impl>(Impl{meshes, images})));
    std::map<SceneObjectId, glm::mat4>                     transforms;
    std::function<void(const scene::NodeDesc&, glm::mat4)> walk = [&](const scene::NodeDesc& node, glm::mat4 parent) {
        if (!node.visible) return;
        auto local = glm::translate(glm::mat4(1), glm::vec3(node.x, node.y, node.z));
        local      = glm::rotate(local, glm::radians(node.yaw), glm::vec3(0, 1, 0));
        local      = glm::rotate(local, glm::radians(node.pitch), glm::vec3(1, 0, 0));
        local      = glm::rotate(local, glm::radians(node.roll), glm::vec3(0, 0, 1));
        local      = glm::scale(local, glm::vec3(node.sx, node.sy, node.sz));
        auto world = parent * local;
        transforms.emplace(node.persistentId, world);
        for (const auto& child : node.children) walk(child, world);
    };
    walk(scene.value().root, glm::mat4(1));
    EvpackGraphicsLoader meshLoader(reader, meshes);
    EvpackImageLoader    imageLoader(reader, images);
    for (const auto& binding : scene.value().renderers) {
        if (!binding.enabled || !transforms.contains(binding.object)) continue;
        auto parameters = material(reader, binding.material, caps);
        if (!parameters) return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(parameters.status());
        const auto meshKey = binding.mesh.format();
        if (!out->impl_->meshLeases.contains(meshKey)) {
            auto loaded = meshLoader.loadMesh(binding.mesh, caps);
            if (!loaded) return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(loaded.status());
            out->impl_->meshLeases.emplace(meshKey, loaded.value().mesh);
        }
        graphics::Texture* image = nullptr;
        if (parameters.value().image) {
            const auto imageKey = parameters.value().image->format();
            if (!out->impl_->imageLeases.contains(imageKey)) {
                auto loaded = imageLoader.load(*parameters.value().image, caps);
                if (!loaded) return Result<std::unique_ptr<EvpackStaticPrefab>>::failure(loaded.status());
                out->impl_->imageLeases.emplace(imageKey, loaded.value().texture);
            }
            image = out->impl_->imageLeases.at(imageKey);
        }
        out->impl_->draws.push_back({transforms.at(binding.object), out->impl_->meshLeases.at(meshKey), image,
                                     std::move(parameters).takeValue()});
    }
    return Result<std::unique_ptr<EvpackStaticPrefab>>::success(std::move(out));
}
Result<void> EvpackStaticPrefab::draw(graphics::Graphics& gfx, const std::array<float, 16>& transform) const {
    if (impl_->released) return fail<void>("prefab resources have been released");
    for (float value : transform)
        if (!std::isfinite(value)) return fail<void>("nonfinite instance transform");
    const auto instance = glm::make_mat4(transform.data());
    for (const auto& draw : impl_->draws) {
        gfx.setMesh3DSurface(graphics::SurfaceMode::Opaque, graphics::BlendMode::Alpha, true, false, 0.5f, "cutoff");
        gfx.setMesh3DMaterial(draw.material.metallic, draw.material.roughness);
        gfx.setMesh3DNormalTexture(nullptr);
        gfx.setMesh3DHeightTexture(nullptr);
        gfx.setMesh3DVirtualTexture(false, 1, 1, 1, 1, 0.f);
        gfx.setMesh3DTexCellBomb(1.f, 0.f, 0.f);
        gfx.setMesh3DParallax(0.f);
        gfx.setMesh3DShadowReceive(true);
        gfx.drawMesh(draw.mesh, instance * draw.transform, draw.image, draw.material.color);
    }
    return Result<void>::success();
}
}  // namespace eve::asset_graphics
