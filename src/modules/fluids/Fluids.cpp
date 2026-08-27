#include "fluids/Fluids.h"

#include "fluids/FluidGpuKernels.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <simplesquirrel/simplesquirrel.hpp>
#include <utility>

namespace eve::fluids {
namespace {

constexpr int kPushCount       = 0;
constexpr int kPushDt          = 1;
constexpr int kPushRadius      = 2;
constexpr int kPushH           = 3;
constexpr int kPushRestDensity = 4;
constexpr int kPushGravityX    = 5;
constexpr int kPushGravityY    = 6;
constexpr int kPushGravityZ    = 7;
constexpr int kPushViscosity   = 8;
constexpr int kPushYield       = 9;
constexpr int kPushCohesion    = 10;
constexpr int kPushAdhesion    = 11;
constexpr int kPushDamping     = 12;
constexpr int kPushMaxVelocity = 13;
constexpr int kPushGridResX    = 14;
constexpr int kPushGridResY    = 15;
constexpr int kPushGridResZ    = 16;
constexpr int kPushGridOriginX = 17;
constexpr int kPushGridOriginY = 18;
constexpr int kPushGridOriginZ = 19;
constexpr int kPushGridCell    = 20;
constexpr int kPushSdfResX     = 21;
constexpr int kPushSdfResY     = 22;
constexpr int kPushSdfResZ     = 23;
constexpr int kPushSdfOriginX  = 24;
constexpr int kPushSdfOriginY  = 25;
constexpr int kPushSdfOriginZ  = 26;
constexpr int kPushSdfCell     = 27;
constexpr int kPushIterations  = 28;
constexpr int kPushMode        = 29;
constexpr int kPushTime        = 30;
constexpr int kPushPbf         = 31;

int groupsFor(int count, int localSize = 64) { return (count + localSize - 1) / localSize; }

}  // namespace

FluidSimulator::FluidSimulator(int maxParticles, const FluidParams& params, bool preferGpu)
    : sim_(maxParticles, params), preferGpu_(preferGpu) {}

FluidSimulator::~FluidSimulator() {
    delete seq_;
    delete shIntegrate_;
    delete shApply_;
    delete shDelta_;
    delete shDensityLambda_;
    delete shBuild_;
    delete shClear_;
    delete stageDens_;
    delete stageVel_;
    delete stagePos_;
    delete bufSdf_;
    delete bufGrad_;
    delete bufLambda_;
    delete bufDens_;
    delete bufNext_;
    delete bufHead_;
    delete bufVel_;
    delete bufPos_;
}

void FluidSimulator::setSdf(const MeshSdf& sdf) {
    sim_.setSdf(sdf);
    grid_     = SimGrid::make(sdf, sim_.params().supportRadius);
    sdfDirty_ = true;
    if (gpuOk_) {
        // Grid and SDF buffers depend on the field; rebuild them.
        delete seq_;
        delete shIntegrate_;
        delete shApply_;
        delete shDelta_;
        delete shDensityLambda_;
        delete shBuild_;
        delete shClear_;
        delete stageDens_;
        delete stageVel_;
        delete stagePos_;
        delete bufSdf_;
        delete bufGrad_;
        delete bufLambda_;
        delete bufDens_;
        delete bufNext_;
        delete bufHead_;
        delete bufVel_;
        delete bufPos_;
        shClear_ = shBuild_ = shDensityLambda_ = shDelta_ = shApply_ = shIntegrate_ = nullptr;
        bufPos_ = bufVel_ = bufHead_ = bufNext_ = bufDens_ = bufLambda_ = bufGrad_ = bufSdf_ = nullptr;
        stagePos_ = stageVel_ = stageDens_ = nullptr;
        seq_                               = nullptr;
        gpuOk_                             = false;
        ensureGpu();
    }
}

int FluidSimulator::spawnDrop(const glm::vec3& center, float radius, int count) {
    return sim_.spawnDrop(center, radius, count);
}

void FluidSimulator::step(float dt) { stepSolver(dt, std::max(1, sim_.params().iterations)); }

void FluidSimulator::stepSolver(float dt, int substeps) {
    if (sim_.particleCount() <= 0) return;
    if (preferGpu_ && !gpuOk_) ensureGpu();
    if (!gpuOk_) {
        sim_.step(dt, substeps);
        return;
    }

    const int   iters = std::max(1, substeps);
    const float sub   = dt / float(iters);

    seq_->begin();
    uploadParticles();
    const int pbf = std::max(0, sim_.params().pbfIterations);
    for (int it = 0; it < iters; ++it) {
        setCommonConstants(shClear_, sub);
        seq_->recordDispatch(shClear_, groupsFor(grid_.cellCount()));
        setCommonConstants(shBuild_, sub);
        seq_->recordDispatch(shBuild_, groupsFor(sim_.maxParticles()));
        setCommonConstants(shIntegrate_, sub);
        seq_->recordDispatch(shIntegrate_, groupsFor(sim_.maxParticles()));
        for (int k = 0; k < pbf; ++k) {
            setCommonConstants(shClear_, sub);
            seq_->recordDispatch(shClear_, groupsFor(grid_.cellCount()));
            setCommonConstants(shBuild_, sub);
            seq_->recordDispatch(shBuild_, groupsFor(sim_.maxParticles()));
            setCommonConstants(shDensityLambda_, sub);
            seq_->recordDispatch(shDensityLambda_, groupsFor(sim_.maxParticles()));
            setCommonConstants(shDelta_, sub);
            seq_->recordDispatch(shDelta_, groupsFor(sim_.maxParticles()));
            setCommonConstants(shApply_, sub);
            seq_->recordDispatch(shApply_, groupsFor(sim_.maxParticles()));
        }
    }
    downloadParticles();
    seq_->submit();

    // Update the CPU mirror so reads reflect GPU state.
    std::vector<float> posF(size_t(sim_.maxParticles()) * 4u);
    std::vector<float> velF(size_t(sim_.maxParticles()) * 4u);
    std::vector<float> densF(size_t(sim_.maxParticles()), 0.f);
    stagePos_->downloadBytes(posF.data(), uint64_t(posF.size()) * sizeof(float));
    stageVel_->downloadBytes(velF.data(), uint64_t(velF.size()) * sizeof(float));
    stageDens_->downloadBytes(densF.data(), uint64_t(densF.size()) * sizeof(float));
    std::vector<FluidParticle>& cpu = sim_.particles();
    for (int i = 0; i < sim_.particleCount(); ++i) {
        const size_t b     = size_t(i) * 4u;
        cpu[size_t(i)].pos = glm::vec3(posF[b], posF[b + 1], posF[b + 2]);
        cpu[size_t(i)].vel = glm::vec3(velF[b], velF[b + 1], velF[b + 2]);
    }
    std::vector<float>& dens = sim_.densities();
    for (int i = 0; i < sim_.particleCount(); ++i) dens[size_t(i)] = densF[size_t(i)];
}

eve::Result<void> FluidSimulator::step(const eve::SimulationStep&              stepValue,
                                       const eve::physics::SimulationSettings& settings) {
    auto valid = eve::physics::detail::validateSimulationStep(stepValue, settings, observation_);
    if (!valid) return valid;
    auto next = eve::physics::detail::advanceSimulationObservation(observation_, stepValue);
    if (!next) return eve::Result<void>::failure(next.status());

    try {
        if (preferGpu_ && !gpuOk_) {
            bool available = false;
            try {
                available = ensureGpu();
            } catch (...) {
                available = false;
            }
            if (!available) {
                backendFallback_        = true;
                backendSelectionStatus_ = eve::Status(
                    eve::StatusCode::Applied,
                    {eve::Diagnostic::warning(
                        eve::DiagnosticCode::Unsupported, "Fluid GPGPU accelerator unavailable; CPU reference selected",
                        "fluids.simulationBackend", {{"selected", "cpu"}, {"fallback", "explicit"}})});
            }
        }
        stepSolver(static_cast<float>(stepValue.delta.seconds()), settings.subStepCount);
    } catch (const std::exception& error) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, std::string("Fluid simulation step failed: ") + error.what(),
            "fluids.simulationBackend.step"));
    } catch (...) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "Fluid simulation step failed with an unknown exception",
            "fluids.simulationBackend.step"));
    }
    observation_ = std::move(next).takeValue();
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> FluidSimulator::restoreObservation(const eve::physics::SimulationObservation& observation) {
    auto valid =
        eve::physics::detail::validateSimulationObservation(observation, "fluids.simulationBackend.restoreObservation");
    if (!valid) return valid;
    observation_ = observation;
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void FluidSimulator::readPositions(std::vector<glm::vec3>& out) const {
    out.clear();
    out.reserve(size_t(sim_.particleCount()));
    for (int i = 0; i < sim_.particleCount(); ++i) out.push_back(sim_.particles()[size_t(i)].pos);
}

void FluidSimulator::readDensities(std::vector<float>& out) const {
    out.clear();
    out.reserve(size_t(sim_.particleCount()));
    for (int i = 0; i < sim_.particleCount(); ++i) out.push_back(sim_.densities()[size_t(i)]);
}

void FluidSimulator::setGravity(float x, float y, float z) { sim_.params().gravity = glm::vec3(x, y, z); }

void FluidSimulator::setViscosity(float viscosity) { sim_.params().viscosity = viscosity; }
void FluidSimulator::setCohesion(float cohesion) { sim_.params().cohesion = cohesion; }
void FluidSimulator::setAdhesion(float adhesion) { sim_.params().adhesion = adhesion; }
void FluidSimulator::setPbfIterations(int passes) { sim_.params().pbfIterations = std::max(0, passes); }
void FluidSimulator::setDamping(float damping) { sim_.params().damping = damping; }
void FluidSimulator::setParticleRadius(float radius) { sim_.params().particleRadius = std::max(1e-4f, radius); }
void FluidSimulator::setSupportRadius(float h) {
    sim_.params().supportRadius = std::max(1e-4f, h);
    grid_                       = SimGrid::make(sim_.sdf(), sim_.params().supportRadius);
}

void FluidSimulator::setSdfSphere(float cx, float cy, float cz, float radius, int res) {
    setSdf(MeshSdf::makeSphere(glm::vec3(cx, cy, cz), radius, glm::ivec3(res, res, res)));
}

bool FluidSimulator::ensureGpu() {
    if (gpuOk_) return true;
    gpgpu_ = eve::gpgpu::Gpgpu::create();
    if (!gpgpu_ || !gpgpu_->isAvailable()) return false;
    if (sim_.sdf().voxelCount() <= 0) return false;

    const int max = sim_.maxParticles();
    try {
        shClear_         = gpgpu_->newShader(kFluidClearGrid);
        shBuild_         = gpgpu_->newShader(kFluidBuildGrid);
        shDensityLambda_ = gpgpu_->newShader(kFluidDensityLambda);
        shDelta_         = gpgpu_->newShader(kFluidComputeDelta);
        shApply_         = gpgpu_->newShader(kFluidApplyDelta);
        shIntegrate_     = gpgpu_->newShader(kFluidIntegrate);
        bufPos_          = gpgpu_->newBuffer(max * 4 * int(sizeof(float)), "storage");
        bufVel_          = gpgpu_->newBuffer(max * 4 * int(sizeof(float)), "storage");
        bufHead_         = gpgpu_->newBuffer(grid_.cellCount() * int(sizeof(int)), "storage");
        bufNext_         = gpgpu_->newBuffer(max * int(sizeof(int)), "storage");
        bufDens_         = gpgpu_->newBuffer(max * int(sizeof(float)), "storage");
        bufLambda_       = gpgpu_->newBuffer(max * int(sizeof(float)), "storage");
        bufGrad_         = gpgpu_->newBuffer(max * 4 * int(sizeof(float)), "storage");
        bufSdf_          = gpgpu_->newBuffer(sim_.sdf().voxelCount() * int(sizeof(float)), "storage");
        stagePos_        = gpgpu_->newBuffer(max * 4 * int(sizeof(float)), "staging");
        stageVel_        = gpgpu_->newBuffer(max * 4 * int(sizeof(float)), "staging");
        stageDens_       = gpgpu_->newBuffer(max * int(sizeof(float)), "staging");
        seq_             = gpgpu_->newSequence();
    } catch (...) {
        return false;
    }
    if (!seq_ || !seq_->isAvailable()) return false;

    gpuOk_ = true;  // uploadSdf() guards on gpuOk_; set it before uploading.
    uploadSdf();
    shClear_->bindBuffer(2, bufHead_);
    shBuild_->bindBuffer(0, bufPos_);
    shBuild_->bindBuffer(2, bufHead_);
    shBuild_->bindBuffer(3, bufNext_);
    shDensityLambda_->bindBuffer(0, bufPos_);
    shDensityLambda_->bindBuffer(2, bufHead_);
    shDensityLambda_->bindBuffer(3, bufNext_);
    shDensityLambda_->bindBuffer(4, bufDens_);
    shDensityLambda_->bindBuffer(6, bufLambda_);
    shDensityLambda_->bindBuffer(7, bufGrad_);
    shDelta_->bindBuffer(0, bufPos_);
    shDelta_->bindBuffer(2, bufHead_);
    shDelta_->bindBuffer(3, bufNext_);
    shDelta_->bindBuffer(6, bufLambda_);
    shDelta_->bindBuffer(7, bufGrad_);
    shApply_->bindBuffer(0, bufPos_);
    shApply_->bindBuffer(2, bufHead_);
    shApply_->bindBuffer(3, bufNext_);
    shApply_->bindBuffer(7, bufGrad_);
    shApply_->bindBuffer(5, bufSdf_);
    shIntegrate_->bindBuffer(0, bufPos_);
    shIntegrate_->bindBuffer(1, bufVel_);
    shIntegrate_->bindBuffer(2, bufHead_);
    shIntegrate_->bindBuffer(3, bufNext_);
    shIntegrate_->bindBuffer(5, bufSdf_);
    return true;
}

void FluidSimulator::uploadSdf() {
    if (!gpuOk_ || !bufSdf_ || !sdfDirty_) return;
    const std::vector<float>& dist = sim_.sdf().distances;
    bufSdf_->writeFloat32s(dist.data(), int(dist.size()));
    sdfDirty_ = false;
}

void FluidSimulator::uploadParticles() {
    if (!gpuOk_) return;
    const int          max = sim_.maxParticles();
    std::vector<float> posF(size_t(max) * 4u, 0.f);
    std::vector<float> velF(size_t(max) * 4u, 0.f);
    const auto&        particles = sim_.particles();
    for (int i = 0; i < sim_.particleCount(); ++i) {
        const size_t b = size_t(i) * 4u;
        posF[b]        = particles[size_t(i)].pos.x;
        posF[b + 1]    = particles[size_t(i)].pos.y;
        posF[b + 2]    = particles[size_t(i)].pos.z;
        velF[b]        = particles[size_t(i)].vel.x;
        velF[b + 1]    = particles[size_t(i)].vel.y;
        velF[b + 2]    = particles[size_t(i)].vel.z;
    }
    seq_->recordUpload(bufPos_, posF.data(), uint64_t(posF.size()) * sizeof(float));
    seq_->recordUpload(bufVel_, velF.data(), uint64_t(velF.size()) * sizeof(float));
}

void FluidSimulator::downloadParticles() {
    if (!gpuOk_) return;
    const int max = sim_.maxParticles();
    seq_->recordDownload(bufPos_, stagePos_, uint64_t(size_t(max) * 4u) * sizeof(float));
    seq_->recordDownload(bufVel_, stageVel_, uint64_t(size_t(max) * 4u) * sizeof(float));
    seq_->recordDownload(bufDens_, stageDens_, uint64_t(size_t(max)) * sizeof(float));
}

void FluidSimulator::setCommonConstants(gpgpu::ComputeShader* shader, float dt) {
    if (!shader) return;
    const FluidParams& p = sim_.params();
    shader->setFloat(kPushCount, float(sim_.particleCount()));
    shader->setFloat(kPushDt, dt);
    shader->setFloat(kPushRadius, p.particleRadius);
    shader->setFloat(kPushH, p.supportRadius);
    shader->setFloat(kPushRestDensity, p.restDensity);
    shader->setFloat(kPushGravityX, p.gravity.x);
    shader->setFloat(kPushGravityY, p.gravity.y);
    shader->setFloat(kPushGravityZ, p.gravity.z);
    shader->setFloat(kPushViscosity, p.viscosity);
    shader->setFloat(kPushYield, p.yieldStress);
    shader->setFloat(kPushCohesion, p.cohesion);
    shader->setFloat(kPushAdhesion, p.adhesion);
    shader->setFloat(kPushDamping, p.damping);
    shader->setFloat(kPushMaxVelocity, p.maxVelocity);
    shader->setFloat(kPushGridResX, float(grid_.dims.x));
    shader->setFloat(kPushGridResY, float(grid_.dims.y));
    shader->setFloat(kPushGridResZ, float(grid_.dims.z));
    shader->setFloat(kPushGridOriginX, grid_.origin.x);
    shader->setFloat(kPushGridOriginY, grid_.origin.y);
    shader->setFloat(kPushGridOriginZ, grid_.origin.z);
    shader->setFloat(kPushGridCell, grid_.cellSize);
    shader->setFloat(kPushSdfResX, float(sim_.sdf().dims.x));
    shader->setFloat(kPushSdfResY, float(sim_.sdf().dims.y));
    shader->setFloat(kPushSdfResZ, float(sim_.sdf().dims.z));
    shader->setFloat(kPushSdfOriginX, sim_.sdf().origin.x);
    shader->setFloat(kPushSdfOriginY, sim_.sdf().origin.y);
    shader->setFloat(kPushSdfOriginZ, sim_.sdf().origin.z);
    shader->setFloat(kPushSdfCell, sim_.sdf().cellSize);
    shader->setFloat(kPushIterations, float(p.iterations));
    shader->setFloat(kPushMode, 0.f);
    shader->setFloat(kPushTime, 0.f);
    shader->setFloat(kPushPbf, float(p.pbfIterations));
}

Fluids::Fluids()  = default;
Fluids::~Fluids() = default;

Module_IMPL(Fluids, new Fluids());

FluidSimulator* Fluids::newSimulator(int maxParticles) {
    auto            sim = std::make_unique<FluidSimulator>(maxParticles, FluidParams{}, true);
    FluidSimulator* raw = sim.get();
    simulators_.push_back(std::move(sim));
    return raw;
}

int Fluids::getSimulatorCount() const { return int(simulators_.size()); }

FluidSurfaceRenderer* Fluids::newSurfaceRenderer(int width, int height) {
    FluidSurfaceParams params;
    params.width              = std::max(8, width);
    params.height             = std::max(8, height);
    auto                  r   = std::make_unique<FluidSurfaceRenderer>(params, true);
    FluidSurfaceRenderer* raw = r.get();
    renderers_.push_back(std::move(r));
    return raw;
}

int Fluids::getRendererCount() const { return int(renderers_.size()); }

void Fluids::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Fluids::create, false);
    expose(cls);

    auto sim = table.addClass<FluidSimulator>(
        "FluidSim", std::function<FluidSimulator*()>([]() -> FluidSimulator* { return nullptr; }), true);
    sim.addFunc("setSdfSphere", &FluidSimulator::setSdfSphere);
    sim.addFunc("setGravity", &FluidSimulator::setGravity);
    sim.addFunc("setViscosity", &FluidSimulator::setViscosity);
    sim.addFunc("setCohesion", &FluidSimulator::setCohesion);
    sim.addFunc("setAdhesion", &FluidSimulator::setAdhesion);
    sim.addFunc("setPbfIterations", &FluidSimulator::setPbfIterations);
    sim.addFunc("setDamping", &FluidSimulator::setDamping);
    sim.addFunc("setParticleRadius", &FluidSimulator::setParticleRadius);
    sim.addFunc("setSupportRadius", &FluidSimulator::setSupportRadius);
    sim.addFunc("spawnDrop", &FluidSimulator::spawnDrop);
    sim.addFunc("step", static_cast<void (FluidSimulator::*)(float)>(&FluidSimulator::step));
    sim.addFunc("getParticleCount", &FluidSimulator::getParticleCount);
    sim.addFunc("getMaxParticles", &FluidSimulator::getMaxParticles);
    sim.addFunc("usingGpu", &FluidSimulator::usingGpu);

    auto surf = table.addClass<FluidSurfaceRenderer>(
        "FluidSurface", std::function<FluidSurfaceRenderer*()>([]() -> FluidSurfaceRenderer* { return nullptr; }),
        true);
    surf.addFunc("render", &FluidSurfaceRenderer::renderFrom);
    surf.addFunc("setMode", &FluidSurfaceRenderer::setMode);
    surf.addFunc("setCamera", &FluidSurfaceRenderer::setCamera);
    surf.addFunc("getWidth", &FluidSurfaceRenderer::getWidth);
    surf.addFunc("getHeight", &FluidSurfaceRenderer::getHeight);
    surf.addFunc("usingGpu", &FluidSurfaceRenderer::usingGpu);
    surf.addFunc("writePpm", &FluidSurfaceRenderer::writePpm);
}

void Fluids::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Fluids::getName);
    cls.addFunc("newSimulator", &Fluids::newSimulator);
    cls.addFunc("getSimulatorCount", &Fluids::getSimulatorCount);
    cls.addFunc("newSurfaceRenderer", &Fluids::newSurfaceRenderer);
    cls.addFunc("getRendererCount", &Fluids::getRendererCount);
}

}  // namespace eve::fluids
