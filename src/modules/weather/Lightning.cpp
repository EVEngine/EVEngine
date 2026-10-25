// eve-hot-path
#include "weather/Lightning.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include "common/Subscription.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/Mesh.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Shader.h"
#include "weather/shaders/WeatherWgsl.h"
#include "weather/shaders/bolt_frag_spv.inc"
#include "weather/shaders/bolt_vert_spv.inc"
namespace eve::weather {
namespace {
Result<void> invalid(const char* message, DiagnosticCode code = DiagnosticCode::InvalidArgument) {
    return Result<void>::failure(Diagnostic::error(code, message, "weather.lightning"));
}
}  // namespace
struct Lightning::Impl {
    graphics::Graphics*                 graphics = nullptr;
    std::weak_ptr<const void>           lifetime;
    Subscription                        retiring;
    ecs::EntityHandle                   entity;
    std::unique_ptr<graphics::Material> material;
    graphics::Shader*                   shader = nullptr;
    std::vector<graphics::Mesh*>        meshes;
    LightningSettings                   settings;
    double                              age     = 0;
    float                               flash   = 0;
    unsigned                            variant = 0;
    bool                                playing = false;
    graphics::Renderable3D*             renderable() const {
        return lifetime.expired() ? nullptr : static_cast<graphics::Renderable3D*>(ecs::try_get(entity));
    }
    void cleanup() {
        if (auto* e = ecs::try_get(entity)) ecs::DestroyEntity(e);
        entity  = {};
        playing = false;
        flash   = 0;
        if (!lifetime.expired()) {
            for (auto* mesh : meshes)
                if (graphics->releaseMesh(mesh)) delete mesh;
            if (shader && graphics->releaseShader(shader)) delete shader;
        }
        meshes.clear();
        shader = nullptr;
        material.reset();
    }
    ~Impl() { cleanup(); }
};
Lightning::Lightning(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Lightning::~Lightning() = default;
Result<std::unique_ptr<Lightning>> Lightning::prepare(graphics::Graphics& graphics, const LightningSettings& settings) {
    auto valid = validateLightning(settings);
    if (!valid) return Result<std::unique_ptr<Lightning>>::failure(valid.status());
    auto impl         = std::make_unique<Impl>();
    impl->graphics    = &graphics;
    impl->lifetime    = graphics.resourceLifetime();
    impl->settings    = settings;
    auto subscription = graphics.onResourcesRetiring([state = impl.get()] { state->cleanup(); });
    if (!subscription) return Result<std::unique_ptr<Lightning>>::failure(subscription.status());
    impl->retiring = std::move(subscription).takeValue();
    try {
        impl->meshes.reserve(static_cast<size_t>(settings.variants));
        if (graphics.getBackendName() == "webgpu")
            impl->shader = graphics.newMeshShaderFromWgsl(shaders::kBoltVertWgsl, shaders::kBoltFragWgsl);
        else
            impl->shader = graphics.newMeshShaderFromSpv({bolt_vert_spv, bolt_vert_spv + bolt_vert_spv_count},
                                                         {bolt_frag_spv, bolt_frag_spv + bolt_frag_spv_count});
        if (!impl->shader)
            return Result<std::unique_ptr<Lightning>>::failure(Diagnostic::error(
                DiagnosticCode::InvalidArgument, "Lightning shader unavailable", "weather.lightning"));
        impl->material = std::make_unique<graphics::Material>();
        auto& m        = *impl->material;
        m.setShader(impl->shader);
        m.setShadingModel("unlit");
        m.setReceiveLight(false);
        m.setReceiveShadow(false);
        m.setCastShadow(false);
        m.setCastOcclusion(false);
        m.setTint(1, 1, 1, 1);
        impl->shader->setPushConstantBlock({});
        for (int i = 0; i < settings.variants; ++i) {
            auto geometry = buildLightningGeometry(settings, static_cast<unsigned>(i));
            if (!geometry) return Result<std::unique_ptr<Lightning>>::failure(geometry.status());
            auto& g    = geometry.value();
            auto* mesh = graphics.newMeshFromArrays(g.positions.data(), g.normals.data(), g.uv.data(),
                                                    static_cast<int>(g.positions.size() / 3), g.indices.data(),
                                                    static_cast<int>(g.indices.size()));
            if (!mesh)
                return Result<std::unique_ptr<Lightning>>::failure(Diagnostic::error(
                    DiagnosticCode::InvalidArgument, "Lightning mesh unavailable", "weather.lightning"));
            impl->meshes.push_back(mesh);
        }
        auto* entity = graphics::Renderable3D::create();
        impl->entity = ecs::handle_of(entity);
        entity->setMesh(impl->meshes.front());
        entity->setMaterial(impl->material.get());
        entity->setVisible(false);
    } catch (const std::exception& e) {
        return Result<std::unique_ptr<Lightning>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, e.what(), "weather.lightning"));
    }
    return Result<std::unique_ptr<Lightning>>::success(std::unique_ptr<Lightning>(new Lightning(std::move(impl))));
}
Result<void> Lightning::setEndpoints(float ax, float ay, float az, float bx, float by, float bz) {
    auto* e = impl_->renderable();
    if (!e) return invalid("Lightning renderer retired", DiagnosticCode::StaleHandle);
    const float dx = bx - ax, dy = by - ay, dz = bz - az;
    const float length = std::hypot(dx, dy, dz);
    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(length) || length < .0001f ||
        length > 1e6f)
        return invalid("Lightning endpoints must be finite and distinct, length <= 1e6");
    e->setPosition(ax, ay, az);
    e->setRotation(std::atan2(dx, dz), std::acos(std::clamp(dy / length, -1.f, 1.f)), 0);
    e->setScale(1, length, 1);
    return Result<void>::success();
}
Result<void> Lightning::setColor(float r, float g, float b) {
    if (!impl_->renderable()) return invalid("Lightning renderer retired", DiagnosticCode::StaleHandle);
    if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b) || r < 0 || g < 0 || b < 0 || r > 16 || g > 16 ||
        b > 16)
        return invalid("Lightning RGB must be finite in [0,16]");
    impl_->material->setTint(r, g, b, 1);
    return Result<void>::success();
}
Result<void> Lightning::trigger() {
    auto* e = impl_->renderable();
    if (!e) return invalid("Lightning renderer retired", DiagnosticCode::StaleHandle);
    e->setMesh(impl_->meshes[impl_->variant++ % impl_->meshes.size()]);
    impl_->playing = true;
    impl_->age     = 0;
    impl_->flash   = 1;
    std::array<float, graphics::Shader::kMaxFloats> block{};
    block[11] = 1;
    impl_->shader->setPushConstantBlock(block);
    e->setVisible(true);
    return Result<void>::success();
}
Result<void> Lightning::update(float dt) {
    if (!std::isfinite(dt) || dt < 0) return invalid("Lightning dt must be finite and nonnegative");
    auto* e = impl_->renderable();
    if (!e) return invalid("Lightning renderer retired", DiagnosticCode::StaleHandle);
    if (!impl_->playing) return Result<void>::success();
    impl_->age   = std::min(impl_->age + dt, static_cast<double>(impl_->settings.duration));
    impl_->flash = lightningEnvelope(static_cast<float>(impl_->age / impl_->settings.duration));
    std::array<float, graphics::Shader::kMaxFloats> block{};
    block[11] = impl_->flash;
    impl_->shader->setPushConstantBlock(block);
    e->setVisible(impl_->flash > .02f);
    if (impl_->age >= impl_->settings.duration) impl_->playing = false;
    return Result<void>::success();
}
void Lightning::stop() {
    impl_->playing = false;
    impl_->flash   = 0;
    if (auto* e = impl_->renderable()) e->setVisible(false);
}
float Lightning::amplitude() const { return impl_->renderable() ? impl_->flash : 0; }
}  // namespace eve::weather
