#include "gpuagents/GpuAgentSimulation.h"
#include <cmath>
#include <functional>
#include <simplesquirrel/simplesquirrel.hpp>
#include <string>
#include <utility>
#include "common/Assert.h"
#include "common/Diagnostic.h"
#include "gpuagents/GpuAgentWorld.h"
#include "gpuagents/GpuAgents.h"
#include "gpuagents/LifeFieldMaterialBinding.h"
#include "gpuagents/SurfaceCapture.h"
#include "gpuagents/solvers/BirdSolver.h"
#include "gpuagents/solvers/FishSolver.h"
#include "gpuagents/solvers/LifeNetworkSolver.h"
#include "gpuagents/solvers/PetalSolver.h"

// GpuAgentSimulation.cpp
namespace eve::gpuagents {

Result<void> GpuAgentSimulation::makeDefaultSolver() {
    switch (profile_.kind) {
        case EffectKind::Fish: solver_ = std::make_unique<FishSolver>(); break;
        case EffectKind::LifeNetwork: solver_ = std::make_unique<LifeNetworkSolver>(); break;
        case EffectKind::Bird: solver_ = std::make_unique<BirdSolver>(); break;
        case EffectKind::Petal: solver_ = std::make_unique<PetalSolver>(); break;
        default:
            return Result<void>::failure(
                Diagnostic::error(DiagnosticCode::InvalidArgument, "unknown EffectKind", "kind", {}, "gpuagents"));
    }
    return Result<void>::success();
}

Result<void> GpuAgentSimulation::configure(const EffectProfile& profile) {
    if (profile.base().maxAgents <= 0) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "maxAgents must be positive",
                                                       "maxAgents", {}, "gpuagents"));
    }
    if (profile.base().fixedDt <= 0.f) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "fixedDt must be positive", "fixedDt", {}, "gpuagents"));
    }
    profile_  = profile;
    capacity_ = profile.base().maxAgents;
    buffers_[0].assign(static_cast<size_t>(capacity_), AgentState{});
    buffers_[1].assign(static_cast<size_t>(capacity_), AgentState{});
    front_         = 0;
    accumulator_   = 0.f;
    simTime_       = 0.f;
    stepCount_     = 0;
    configured_    = true;
    auto solverRes = makeDefaultSolver();
    if (!solverRes.ok()) {
        configured_ = false;
        return solverRes;
    }
    return Result<void>::success();
}

Result<void> GpuAgentSimulation::setSolver(std::unique_ptr<IAgentSolver> solver) {
    if (!solver) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "solver is null", "solver", {}, "gpuagents"));
    }
    if (configured_ && solver->kind() != profile_.kind) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "solver kind mismatch", "solver", {}, "gpuagents"));
    }
    solver_ = std::move(solver);
    return Result<void>::success();
}

Result<void> GpuAgentSimulation::initialize(int count, const std::function<AgentState(int)>& spawnFn) {
    if (!configured_) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "simulation not configured", "configure", {}, "gpuagents"));
    }
    if (!spawnFn) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "spawnFn is empty", "spawnFn", {}, "gpuagents"));
    }
    if (count < 0 || count > capacity_) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "initialize count out of range",
                                                       "count", {}, "gpuagents"));
    }
    auto& buf = buffers_[front_];
    for (int i = 0; i < capacity_; ++i) {
        if (i < count) {
            AgentState s                = spawnFn(i);
            s.alive                     = 1;
            buf[static_cast<size_t>(i)] = s;
        } else {
            buf[static_cast<size_t>(i)] = AgentState{};
        }
    }
    buffers_[1 - front_] = buf;
    accumulator_         = 0.f;
    return Result<void>::success();
}

void GpuAgentSimulation::reset() {
    if (!configured_) return;
    for (auto& b : buffers_) {
        for (auto& s : b) s = AgentState{};
    }
    accumulator_ = 0.f;
    simTime_     = 0.f;
    stepCount_   = 0;
}

int GpuAgentSimulation::aliveCount() const {
    int n = 0;
    for (const auto& s : buffers_[front_]) {
        if (s.alive) ++n;
    }
    return n;
}

Result<void> GpuAgentSimulation::step(float dt, const EnvironmentSnapshot& env) {
    if (!configured_ || !solver_) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                       "simulation not configured", "configure", {}, "gpuagents"));
    }
    EV_PARAM_CHECK(dt >= 0.f);
    accumulator_ += dt;
    const float fixed = profile_.base().fixedDt;
    int         guard = 0;
    while (accumulator_ + 1e-8f >= fixed && guard < 8) {
        const int read  = front_;
        const int write = 1 - front_;
        solver_->step(buffers_[read], buffers_[write], profile_, env, fixed);
        front_ = write;
        accumulator_ -= fixed;
        simTime_ += fixed;
        ++stepCount_;
        ++guard;
    }
    return Result<void>::success();
}

}  // namespace eve::gpuagents

// GpuAgentWorld.cpp
namespace eve::gpuagents {

Result<void> GpuAgentWorld::registerBackend(std::string name, EffectBackend* backend) {
    if (name.empty()) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "backend name is empty", "name", {}, "gpuagents"));
    }
    if (!backend) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "backend is null", "backend", {}, "gpuagents"));
    }
    if (backends_.count(name) != 0) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::AlreadyExists, "backend name already registered", name, {}, "gpuagents"));
    }
    backends_.emplace(std::move(name), backend);
    return Result<void>::success();
}

Result<void> GpuAgentWorld::unregisterBackend(const std::string& name) {
    if (backends_.erase(name) == 0) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "backend not registered", name, {}, "gpuagents"));
    }
    return Result<void>::success();
}

EffectBackend* GpuAgentWorld::find(const std::string& name) const {
    const auto it = backends_.find(name);
    return it == backends_.end() ? nullptr : it->second;
}

EnvironmentSnapshot GpuAgentWorld::makeSnapshot(bool includeSurface) const {
    EnvironmentSnapshot snap;
    snap.waterCurrent = waterCurrent_;
    snap.windVelocity = wind_;
    snap.groundY      = groundY_;
    snap.ceilingY     = ceilingY_;
    snap.goals        = goals_;
    snap.dangers      = dangers_;
    snap.nutrients    = nutrients_;
    snap.obstacles    = &obstacles_;
    snap.surface      = includeSurface ? const_cast<SurfaceField*>(&surface_) : nullptr;
    return snap;
}

Result<void> GpuAgentWorld::stepAll(float dt) {
    for (auto& [name, backend] : backends_) {
        (void)name;
        if (!backend) {
            return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation,
                                                           "registered backend is null", name, {}, "gpuagents"));
        }
        const bool          needSurface = backend->kind() == EffectKind::LifeNetwork;
        EnvironmentSnapshot snap        = makeSnapshot(needSurface);
        auto                stepped     = backend->step(dt, snap);
        if (!stepped.ok()) return stepped;
    }
    return Result<void>::success();
}

}  // namespace eve::gpuagents

// GpuAgents.cpp
namespace eve::gpuagents {
namespace {

glm::vec3 unitFromIndex(int i, std::uint32_t seed) {
    const float a     = static_cast<float>((static_cast<std::uint32_t>(i) * 1103515245u + seed) & 0xFFFFu) / 65535.f;
    const float b     = static_cast<float>(((static_cast<std::uint32_t>(i) * 12345u) ^ seed) & 0xFFFFu) / 65535.f;
    const float theta = a * 6.2831853f;
    const float u     = b * 2.f - 1.f;
    const float r     = std::sqrt(std::max(0.f, 1.f - u * u));
    return glm::vec3(r * std::cos(theta), u, r * std::sin(theta));
}

}  // namespace

Module_IMPL(GpuAgents, new GpuAgents());

GpuAgentWorld* GpuAgents::newWorld() { return new GpuAgentWorld(); }

EffectProfile GpuAgents::makeProfile(int kind, int maxAgents) const {
    switch (static_cast<EffectKind>(kind)) {
        case EffectKind::LifeNetwork: return EffectProfile::makeLife(maxAgents);
        case EffectKind::Bird: return EffectProfile::makeBird(maxAgents);
        case EffectKind::Petal: return EffectProfile::makePetal(maxAgents);
        case EffectKind::Fish:
        default: return EffectProfile::makeFish(maxAgents);
    }
}

EffectBackend* GpuAgents::newBackend(int kind, int maxAgents) {
    auto backend = std::make_unique<EffectBackend>();
    auto profile = makeProfile(kind, maxAgents);
    auto cfg     = backend->configure(profile);
    if (!cfg.ok()) return nullptr;
    return backend.release();
}

Result<void> GpuAgents::spawnCloud(EffectBackend* backend, int count, float centerX, float centerY, float centerZ,
                                   float spread) const {
    if (!backend) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "backend is null", "backend", {}, "gpuagents"));
    }
    if (!backend->simulation().isConfigured()) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::PreconditionViolation, "backend not configured",
                                                       "configure", {}, "gpuagents"));
    }
    const glm::vec3  center(centerX, centerY, centerZ);
    const auto&      base = backend->simulation().profile().base();
    const EffectKind kind = backend->kind();
    return backend->simulation().initialize(count, [&](int i) {
        AgentState      s;
        const glm::vec3 dir = unitFromIndex(i, base.seed);
        s.position          = center + dir * spread;
        switch (kind) {
            case EffectKind::Fish: s.velocity = dir * 2.f; break;
            case EffectKind::Bird: s.velocity = glm::normalize(dir + glm::vec3(0.f, 0.2f, 0.f)) * 6.f; break;
            case EffectKind::Petal:
                s.velocity      = glm::vec3(dir.x, -1.f, dir.z);
                s.customData[1] = 0.75f + 0.5f * (static_cast<float>(i % 10) / 10.f);
                s.rotation      = lookRotation(glm::vec3(0.f, -1.f, 0.f));
                break;
            case EffectKind::LifeNetwork:
                s.position.y = centerY;
                s.velocity   = glm::normalize(glm::vec3(dir.x, 0.f, dir.z) + glm::vec3(1e-3f, 0.f, 0.f)) * 1.5f;
                break;
        }
        s.rotation = lookRotation(glm::length(s.velocity) > 1e-4f ? s.velocity : glm::vec3(0.f, 0.f, 1.f));
        return s;
    });
}

Result<void> GpuAgents::captureSurface(GpuAgentWorld* world, const std::vector<float>& positions,
                                       const std::vector<std::uint32_t>& indices, float originX, float originY,
                                       float originZ, float worldSize, int resolution) const {
    if (!world) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "world is null", "world", {}, "gpuagents"));
    }
    if (positions.size() % 3 != 0) {
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "positions must be interleaved xyz floats", "positions", {}, "gpuagents"));
    }
    std::vector<glm::vec3> verts;
    verts.reserve(positions.size() / 3);
    for (size_t i = 0; i + 2 < positions.size(); i += 3) {
        verts.emplace_back(positions[i], positions[i + 1], positions[i + 2]);
    }
    return SurfaceCapture::captureFromTriangles(world->surface(), verts, indices, glm::vec3(originX, originY, originZ),
                                                worldSize, resolution, originY);
}

Result<void> GpuAgents::syncLifeFieldBinding(GpuAgentWorld* world, LifeFieldMaterialBinding* binding) const {
    if (!world || !binding) {
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "world or binding is null", "binding", {}, "gpuagents"));
    }
    return binding->syncFrom(world->surface());
}

void GpuAgents::expose(ssq::Table& table) {
    auto cls = table.addClass(name, GpuAgents::create, false);
    expose(cls);

    auto world = table.addClass<GpuAgentWorld>(
        "GpuAgentWorld", std::function<GpuAgentWorld*()>([]() -> GpuAgentWorld* { return nullptr; }), true);
    world.addFunc("setWaterCurrent", &GpuAgentWorld::setWaterCurrent);
    world.addFunc("setWind", &GpuAgentWorld::setWind);
    world.addFunc("setVerticalBounds", &GpuAgentWorld::setVerticalBounds);
    world.addFunc("backendCount", &GpuAgentWorld::backendCount);
    world.addFunc("stepAll", [](GpuAgentWorld* w, float dt) {
        if (!w) return false;
        return w->stepAll(dt).ok();
    });
    world.addFunc("bakeEmptyObstacles",
                  [](GpuAgentWorld* w, float ox, float oy, float oz, int dx, int dy, int dz, float cell) {
                      if (!w) return;
                      w->obstacles().bakeEmpty(glm::vec3(ox, oy, oz), glm::ivec3(dx, dy, dz), cell);
                  });
    world.addFunc("carveSphere", [](GpuAgentWorld* w, float x, float y, float z, float r) {
        if (!w) return;
        w->obstacles().carveSphere(glm::vec3(x, y, z), r);
    });
    world.addFunc("initFlatSurface", [](GpuAgentWorld* w, float ox, float oy, float oz, float size, int res) {
        if (!w) return;
        w->surface().initFlat(glm::vec3(ox, oy, oz), size, res, oy);
    });

    auto backend = table.addClass<EffectBackend>(
        "GpuAgentEffectBackend", std::function<EffectBackend*()>([]() -> EffectBackend* { return nullptr; }), true);
    backend.addFunc("kind", [](EffectBackend* b) { return b ? static_cast<int>(b->kind()) : -1; });
    backend.addFunc("aliveCount", [](EffectBackend* b) { return b ? b->simulation().aliveCount() : 0; });
    backend.addFunc("instanceCount", [](EffectBackend* b) { return b ? b->renderer().instanceCount() : 0; });
    backend.addFunc("reset", [](EffectBackend* b) {
        if (b) b->reset();
    });
    backend.addFunc("step", [](EffectBackend* b, GpuAgentWorld* w, float dt) {
        if (!b || !w) return false;
        const bool needSurface = b->kind() == EffectKind::LifeNetwork;
        return b->step(dt, w->makeSnapshot(needSurface)).ok();
    });
}

void GpuAgents::expose(ssq::Class& cls) {
    cls.addFunc("newWorld", &GpuAgents::newWorld);
    cls.addFunc("newBackend", &GpuAgents::newBackend);
    cls.addFunc("spawnCloud", [](GpuAgents* m, EffectBackend* b, int count, float x, float y, float z, float spread) {
        if (!m) return false;
        return m->spawnCloud(b, count, x, y, z, spread).ok();
    });
    cls.addFunc("registerBackend", [](GpuAgents*, GpuAgentWorld* w, const std::string& name, EffectBackend* b) {
        if (!w || !b) return false;
        // Borrowed: script / VM retains ownership of the backend.
        return w->registerBackend(name, b).ok();
    });
    cls.addFunc("captureSurface", [](GpuAgents* m, GpuAgentWorld* w, ssq::Array positions, ssq::Array indices, float ox,
                                     float oy, float oz, float size, int res) {
        if (!m || !w) return false;
        std::vector<float> pos;
        pos.reserve(positions.size());
        for (size_t i = 0; i < positions.size(); ++i) pos.push_back(positions.get<float>(i));
        std::vector<std::uint32_t> idx;
        idx.reserve(indices.size());
        for (size_t i = 0; i < indices.size(); ++i) idx.push_back(static_cast<std::uint32_t>(indices.get<int>(i)));
        return m->captureSurface(w, pos, idx, ox, oy, oz, size, res).ok();
    });
}

}  // namespace eve::gpuagents
