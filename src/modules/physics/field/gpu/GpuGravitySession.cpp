// eve-hot-path
#include "physics/field/gpu/GpuGravitySession.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <utility>
#include "gpgpu/ComputeProgram.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "graphics/Graphics.h"
#include "graphics/multiscale/SphereView.h"
#include "physics/field/gpu/copy_appearances_spv.inc"
#include "physics/field/gpu/evaluate_spv.inc"
#include "physics/field/gpu/final_spv.inc"
#include "physics/field/gpu/project_spv.inc"
#include "physics/field/gpu/reduce_spv.inc"

namespace eve::physics::field::gpu {
namespace {
using Row = std::array<float, 4>;
static_assert(sizeof(GpuSource) == sizeof(Row));
struct Control {
    std::uint64_t generation = 0;
    bool          pending    = false;
};
template <class T>
Result<T> fail(DiagnosticCode code, const char* message) {
    return Result<T>::failure(Diagnostic::error(code, message, "physics.field.gpu"));
}
bool finite3(const std::array<float, 3>& value) {
    return std::all_of(value.begin(), value.end(), [](float element) { return std::isfinite(element); });
}
double dot3(const std::array<float, 3>& a, const std::array<float, 3>& b) {
    return double(a[0]) * b[0] + double(a[1]) * b[1] + double(a[2]) * b[2];
}
bool validAppearance(const GpuSphereAppearance& value) {
    return std::isfinite(value.radius) && value.radius > 0 && std::isfinite(value.depthExponent) &&
           std::abs(value.depthExponent) <= 2048 && finite3(value.color) &&
           std::all_of(value.color.begin(), value.color.end(), [](float component) { return component >= 0; });
}
std::array<Row, 2> packAppearance(const GpuSphereAppearance& value) {
    return {{{value.radius, value.depthExponent, 0, 0}, {value.color[0], value.color[1], value.color[2], 0}}};
}
std::unique_ptr<gpgpu::ComputeShader> makeShader(const std::uint32_t* begin, std::size_t count) {
    std::vector<std::uint32_t> words(begin, begin + count);
    auto                       built = gpgpu::createComputeShader(words);
    if (!built.ok()) throw std::runtime_error(built.status().describe());
    return std::move(built).takeValue();
}
std::unique_ptr<gpgpu::GpuBuffer> makeBuffer(gpgpu::Gpgpu& compute, std::size_t bytes, const char* usage = "storage") {
    auto result = std::unique_ptr<gpgpu::GpuBuffer>(compute.newBuffer(static_cast<int>(bytes), usage));
    if (!result) throw std::runtime_error("GPU buffer allocation returned no handle");
    return result;
}
}  // namespace

struct PreparedGpuGravityStep::Impl {
    std::weak_ptr<Control> owner;
    FieldStamp             base, next;
    std::uint64_t          generation       = 0;
    float                  nextDisplacement = 0;
    bool                   armed            = true;
    ~Impl() {
        if (!armed) return;
        if (auto control = owner.lock(); control && control->generation == generation) control->pending = false;
    }
};

struct GpuGravitySession::Impl {
    gpgpu::Gpgpu*                                    compute = nullptr;
    std::array<std::unique_ptr<gpgpu::GpuBuffer>, 2> sources, targets;
    std::unique_ptr<gpgpu::GpuBuffer> partials, summaries, admission, staging, packets, appearances, appearanceSpare;
    std::unique_ptr<gpgpu::ComputeShader> reduce, final, evaluate, project, copyAppearances;
    std::unique_ptr<gpgpu::Sequence>      sequence;
    std::weak_ptr<const void>             graphicsLifetime;
    std::shared_ptr<Control>              control = std::make_shared<Control>();
    FieldStamp                            current;
    Duration                              fixedStep;
    GpuFieldPolicy                        policy;
    std::size_t                           sourceCount = 0, targetCount = 0;
    int                                   groups       = 0;
    unsigned                              phase        = 0;
    float                                 displacement = 0;
};

PreparedGpuGravityStep::PreparedGpuGravityStep(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
PreparedGpuGravityStep::~PreparedGpuGravityStep()                                            = default;
PreparedGpuGravityStep::PreparedGpuGravityStep(PreparedGpuGravityStep&&) noexcept            = default;
PreparedGpuGravityStep& PreparedGpuGravityStep::operator=(PreparedGpuGravityStep&&) noexcept = default;
GpuGravitySession::GpuGravitySession(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GpuGravitySession::~GpuGravitySession() = default;

Result<std::unique_ptr<GpuGravitySession>> GpuGravitySession::create(
    gpgpu::Gpgpu& compute, graphics::Graphics& graphics, std::span<const GpuSource> sources,
    std::span<const GpuMotion> targets, FieldStamp stamp, Duration fixedStep, GpuFieldPolicy policy) {
    if (graphics.getBackendName() != "vulkan" || graphics.resourceLifetime().expired() || !compute.isAvailable())
        return fail<std::unique_ptr<GpuGravitySession>>(DiagnosticCode::Unsupported,
                                                        "Vulkan GPGPU provider unavailable");
    if (sources.empty() || targets.empty() || sources.size() > policy.maxSources ||
        targets.size() > policy.maxTargets || sources.size() > 1000000 || targets.size() > 1000000 ||
        fixedStep.nanoseconds() <= 0 || !std::isfinite(fixedStep.seconds()) ||
        !std::isfinite(static_cast<float>(fixedStep.seconds())) || !std::isfinite(policy.absoluteAccelerationError) ||
        policy.absoluteAccelerationError <= 0 || !std::isfinite(policy.clusterRadius) || policy.clusterRadius <= 0 ||
        !std::isfinite(policy.maxSourceDisplacement) || policy.maxSourceDisplacement < 0 ||
        policy.clusterRadius <= 2 * policy.maxSourceDisplacement)
        return fail<std::unique_ptr<GpuGravitySession>>(DiagnosticCode::InvalidArgument,
                                                        "Invalid GPU field inputs or policy");

    std::array<double, 8>                mass{};
    std::array<std::array<double, 3>, 8> moments{};
    for (const auto& source : sources) {
        if (!finite3(source.position) || !std::isfinite(source.mass) || source.mass <= 0 ||
            std::abs(source.position[0]) <= policy.maxSourceDisplacement)
            return fail<std::unique_ptr<GpuGravitySession>>(DiagnosticCode::InvalidArgument,
                                                            "Invalid or cell-crossing source");
        const unsigned cell = unsigned(source.position[0] >= 0) | (unsigned(source.position[1] >= 0) << 1) |
                              (unsigned(source.position[2] >= 0) << 2);
        mass[cell] += source.mass;
        for (unsigned axis = 0; axis < 3; ++axis)
            moments[cell][axis] += static_cast<double>(source.position[axis]) * source.mass;
    }
    for (const auto& source : sources) {
        const unsigned cell = unsigned(source.position[0] >= 0) | (unsigned(source.position[1] >= 0) << 1) |
                              (unsigned(source.position[2] >= 0) << 2);
        if (!std::isfinite(mass[cell]) || mass[cell] <= 0)
            return fail<std::unique_ptr<GpuGravitySession>>(DiagnosticCode::Unsupported, "Source mass range exceeded");
        const double distance = std::hypot(source.position[0] - moments[cell][0] / mass[cell],
                                           source.position[1] - moments[cell][1] / mass[cell],
                                           source.position[2] - moments[cell][2] / mass[cell]);
        if (!std::isfinite(distance) || distance + 2 * policy.maxSourceDisplacement > policy.clusterRadius)
            return fail<std::unique_ptr<GpuGravitySession>>(DiagnosticCode::Unsupported,
                                                            "Source cluster radius exceeded");
    }
    for (const auto& target : targets)
        if (!finite3(target.position) || !finite3(target.velocity))
            return fail<std::unique_ptr<GpuGravitySession>>(DiagnosticCode::InvalidArgument, "Invalid target state");

    try {
        auto impl              = std::make_unique<Impl>();
        impl->compute          = &compute;
        impl->sourceCount      = sources.size();
        impl->targetCount      = targets.size();
        impl->groups           = static_cast<int>((sources.size() + 255) / 256);
        impl->current          = stamp;
        impl->fixedStep        = fixedStep;
        impl->policy           = policy;
        impl->graphicsLifetime = graphics.resourceLifetime();
        const auto sourceBytes = sources.size() * sizeof(GpuSource);
        const auto targetBytes = targets.size() * 2 * sizeof(Row);
        for (auto& buffer : impl->sources) buffer = makeBuffer(compute, sourceBytes);
        for (auto& buffer : impl->targets) buffer = makeBuffer(compute, targetBytes);
        impl->partials  = makeBuffer(compute, static_cast<std::size_t>(impl->groups) * 8 * sizeof(Row));
        impl->summaries = makeBuffer(compute, 8 * sizeof(Row));
        impl->admission = makeBuffer(compute, sizeof(std::uint32_t));
        impl->staging   = makeBuffer(compute, sizeof(std::uint32_t), "staging");
        impl->packets   = makeBuffer(compute, targets.size() * 64 + 32768);
        impl->sources[0]->uploadBytes(sources.data(), sourceBytes);
        std::vector<Row> packed(targets.size() * 2);
        for (std::size_t i = 0; i < targets.size(); ++i) {
            for (unsigned axis = 0; axis < 3; ++axis) {
                packed[i * 2][axis]     = targets[i].position[axis];
                packed[i * 2 + 1][axis] = targets[i].velocity[axis];
            }
        }
        impl->targets[0]->uploadBytes(packed.data(), targetBytes);
        impl->reduce          = makeShader(gpu_field_reduce_spv, gpu_field_reduce_spv_count);
        impl->final           = makeShader(gpu_field_final_spv, gpu_field_final_spv_count);
        impl->evaluate        = makeShader(gpu_field_evaluate_spv, gpu_field_evaluate_spv_count);
        impl->project         = makeShader(gpu_field_project_spv, gpu_field_project_spv_count);
        impl->copyAppearances = makeShader(gpu_field_copy_appearances_spv, gpu_field_copy_appearances_spv_count);
        impl->copyAppearances->setFloat(0, static_cast<float>(targets.size()));
        impl->sequence.reset(compute.newSequence());
        if (!impl->sequence || !impl->sequence->isAvailable())
            return fail<std::unique_ptr<GpuGravitySession>>(DiagnosticCode::Unsupported,
                                                            "GPU command sequence unavailable");
        for (auto* shader : {impl->reduce.get(), impl->final.get(), impl->evaluate.get()}) {
            shader->setFloat(0, static_cast<float>(sources.size()));
            shader->setFloat(1, static_cast<float>(impl->groups));
            shader->setFloat(2, static_cast<float>(fixedStep.seconds()));
            shader->setFloat(3, static_cast<float>(targets.size()));
            shader->setFloat(4, policy.absoluteAccelerationError);
            shader->setFloat(6, policy.clusterRadius);
        }
        impl->reduce->bindBuffer(1, impl->partials.get());
        impl->final->bindBuffer(1, impl->partials.get());
        impl->final->bindBuffer(2, impl->summaries.get());
        impl->evaluate->bindBuffer(2, impl->summaries.get());
        impl->evaluate->bindBuffer(5, impl->admission.get());
        impl->project->bindBuffer(1, impl->packets.get());
        impl->project->bindBuffer(2, impl->admission.get());
        impl->project->bindBuffer(3, impl->packets.get());
        return Result<std::unique_ptr<GpuGravitySession>>::success(
            std::unique_ptr<GpuGravitySession>(new GpuGravitySession(std::move(impl))));
    } catch (const std::exception& error) {
        return Result<std::unique_ptr<GpuGravitySession>>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "physics.field.gpu"));
    }
}

Result<PreparedGpuGravityStep> GpuGravitySession::prepareStep(SimulationStep step, float sourceShift) {
    if (!impl_ || impl_->graphicsLifetime.expired())
        return fail<PreparedGpuGravityStep>(DiagnosticCode::StaleHandle, "GPU field provider retired");
    if (impl_->control->pending)
        return fail<PreparedGpuGravityStep>(DiagnosticCode::Conflict, "Previous GPU candidate still pending");
    const auto  tick             = impl_->current.tick.incremented();
    const auto  revision         = impl_->current.revision.incremented();
    const float nextDisplacement = impl_->displacement + sourceShift;
    if (!tick || !revision || step.tick != *tick || step.delta != impl_->fixedStep || !std::isfinite(sourceShift) ||
        !std::isfinite(nextDisplacement) || std::abs(nextDisplacement) > impl_->policy.maxSourceDisplacement)
        return fail<PreparedGpuGravityStep>(DiagnosticCode::InvalidArgument, "Invalid tick, dt or source motion bound");
    try {
        auto&          state     = *impl_;
        const unsigned current   = state.phase;
        const unsigned candidate = 1 - current;
        state.reduce->setFloat(5, sourceShift);
        state.reduce->bindBuffer(0, state.sources[current].get());
        state.reduce->bindBuffer(6, state.sources[candidate].get());
        state.evaluate->bindBuffer(3, state.targets[current].get());
        state.evaluate->bindBuffer(4, state.targets[candidate].get());
        const std::uint32_t clear = 0;
        state.sequence->begin();
        state.sequence->recordUpload(state.admission.get(), &clear, sizeof(clear));
        state.sequence->recordDispatch(state.reduce.get(), state.groups);
        state.sequence->recordDispatch(state.final.get(), 8);
        state.sequence->recordDispatch(state.evaluate.get(), static_cast<int>((state.targetCount + 255) / 256));
        state.sequence->recordDownload(state.admission.get(), state.staging.get(), sizeof(clear));
        state.sequence->submit();
        std::uint32_t rejected = 0;
        state.staging->downloadBytes(&rejected, sizeof(rejected));
        if (rejected != 0)
            return fail<PreparedGpuGravityStep>(DiagnosticCode::Unsupported, "GPU field approximation rejected target");
        auto candidateStep              = std::make_unique<PreparedGpuGravityStep::Impl>();
        candidateStep->owner            = state.control;
        candidateStep->base             = state.current;
        candidateStep->next             = {*tick, *revision};
        candidateStep->nextDisplacement = nextDisplacement;
        candidateStep->generation       = ++state.control->generation;
        state.control->pending          = true;
        return Result<PreparedGpuGravityStep>::success(PreparedGpuGravityStep(std::move(candidateStep)));
    } catch (const std::exception& error) {
        return Result<PreparedGpuGravityStep>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "physics.field.gpu"));
    }
}

Result<void> GpuGravitySession::commit(PreparedGpuGravityStep step) {
    if (!impl_ || !step.impl_ || impl_->graphicsLifetime.expired() || step.impl_->owner.lock() != impl_->control ||
        !impl_->control->pending || step.impl_->generation != impl_->control->generation ||
        step.impl_->base != impl_->current)
        return fail<void>(DiagnosticCode::StaleHandle, "GPU candidate is foreign, consumed or stale");
    impl_->phase            = 1 - impl_->phase;
    impl_->current          = step.impl_->next;
    impl_->displacement     = step.impl_->nextDisplacement;
    impl_->control->pending = false;
    step.impl_->armed       = false;
    return Result<void>::success();
}

Result<void> GpuGravitySession::updateSphereBatch(graphics::multiscale::SphereBatch& batch,
                                                  const GpuSphereProjection&         projection) {
    if (!impl_ || impl_->graphicsLifetime.expired())
        return fail<void>(DiagnosticCode::StaleHandle, "GPU field provider retired");
    if (!finite3(projection.camera) || !finite3(projection.right) || !finite3(projection.up) ||
        !finite3(projection.forward) || !finite3(projection.color) || !std::isfinite(projection.tangentX) ||
        projection.tangentX <= 0 || !std::isfinite(projection.tangentY) || projection.tangentY <= 0 ||
        !std::isfinite(projection.radius) || projection.radius <= 0 || !std::isfinite(projection.depthExponent) ||
        std::abs(projection.depthExponent) > 2048 ||
        std::any_of(projection.color.begin(), projection.color.end(), [](float value) { return value < 0; }))
        return fail<void>(DiagnosticCode::InvalidArgument, "Invalid GPU sphere projection");
    for (const auto* axis : {&projection.right, &projection.up, &projection.forward})
        if (std::abs(dot3(*axis, *axis) - 1.0) > 1e-3)
            return fail<void>(DiagnosticCode::InvalidArgument, "Camera basis is not unit length");
    if (std::abs(dot3(projection.right, projection.up)) > 1e-3 ||
        std::abs(dot3(projection.right, projection.forward)) > 1e-3 ||
        std::abs(dot3(projection.up, projection.forward)) > 1e-3)
        return fail<void>(DiagnosticCode::InvalidArgument, "Camera basis is not orthogonal");
    try {
        auto& state  = *impl_;
        auto* shader = state.project.get();
        shader->setFloat(0, static_cast<float>(state.targetCount));
        for (unsigned axis = 0; axis < 3; ++axis) {
            shader->setFloat(1 + axis, projection.camera[axis]);
            shader->setFloat(4 + axis, projection.right[axis]);
            shader->setFloat(7 + axis, projection.up[axis]);
            shader->setFloat(10 + axis, projection.forward[axis]);
            shader->setFloat(17 + axis, projection.color[axis]);
        }
        shader->setFloat(13, projection.tangentX);
        shader->setFloat(14, projection.tangentY);
        shader->setFloat(15, projection.radius);
        shader->setFloat(16, projection.depthExponent);
        shader->setFloat(20, state.appearances ? 1.f : 0.f);
        shader->bindBuffer(0, state.targets[state.phase].get());
        const std::uint32_t clear = 0;
        state.sequence->begin();
        state.sequence->recordUpload(state.admission.get(), &clear, sizeof(clear));
        state.sequence->recordDispatch(shader, static_cast<int>((state.targetCount + 255) / 256));
        state.sequence->recordDownload(state.admission.get(), state.staging.get(), sizeof(clear));
        state.sequence->submit();
        std::uint32_t rejected = 0;
        state.staging->downloadBytes(&rejected, sizeof(rejected));
        if (rejected != 0) return fail<void>(DiagnosticCode::Unsupported, "GPU sphere projection rejected target");
        return batch.updateResidentPackets(state.packets->residentView(),
                                           static_cast<std::uint32_t>(state.targetCount));
    } catch (const std::exception& error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "physics.field.gpu"));
    }
}

Result<void> GpuGravitySession::setSphereAppearances(std::span<const GpuSphereAppearance> appearances) {
    if (!impl_ || impl_->graphicsLifetime.expired())
        return fail<void>(DiagnosticCode::StaleHandle, "GPU field provider retired");
    if (appearances.size() != impl_->targetCount)
        return fail<void>(DiagnosticCode::InvalidArgument, "GPU sphere appearance count differs from targets");
    try {
        std::vector<Row> packed(appearances.size() * 2);
        for (std::size_t i = 0; i < appearances.size(); ++i) {
            const auto& appearance = appearances[i];
            if (!validAppearance(appearance))
                return fail<void>(DiagnosticCode::InvalidArgument, "Invalid GPU sphere appearance");
            const auto rows   = packAppearance(appearance);
            packed[i * 2]     = rows[0];
            packed[i * 2 + 1] = rows[1];
        }
        auto next = makeBuffer(*impl_->compute, packed.size() * sizeof(Row));
        next->uploadBytes(packed.data(), packed.size() * sizeof(Row));
        impl_->project->bindBuffer(3, next.get());
        impl_->appearances = std::move(next);
        impl_->appearanceSpare.reset();
        return Result<void>::success();
    } catch (const std::exception& error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "physics.field.gpu"));
    }
}

Result<void> GpuGravitySession::editSphereAppearances(std::span<const GpuSphereAppearanceEdit> edits) {
    if (!impl_ || impl_->graphicsLifetime.expired())
        return fail<void>(DiagnosticCode::StaleHandle, "GPU field provider retired");
    if (!impl_->appearances)
        return fail<void>(DiagnosticCode::InvalidArgument, "GPU sphere appearances were not initialized");
    if (edits.empty()) return Result<void>::success();
    std::uint32_t previous = 0;
    bool          first    = true;
    for (const auto& edit : edits) {
        if (edit.targetIndex >= impl_->targetCount || (!first && edit.targetIndex <= previous) ||
            !validAppearance(edit.appearance))
            return fail<void>(DiagnosticCode::InvalidArgument, "Invalid or unsorted GPU sphere appearance edit");
        previous = edit.targetIndex;
        first    = false;
    }
    try {
        auto& state = *impl_;
        if (!state.appearanceSpare)
            state.appearanceSpare = makeBuffer(*state.compute, state.targetCount * 2 * sizeof(Row));
        auto* copy = state.copyAppearances.get();
        copy->bindBuffer(0, state.appearances.get());
        copy->bindBuffer(1, state.appearanceSpare.get());
        std::unique_ptr<gpgpu::Sequence> editSequence(state.compute->newSequence());
        if (!editSequence || !editSequence->isAvailable())
            return fail<void>(DiagnosticCode::Unsupported, "GPU appearance edit sequence unavailable");
        editSequence->begin();
        editSequence->recordDispatch(copy, static_cast<int>((state.targetCount + 255) / 256));
        for (const auto& edit : edits) {
            const auto rows = packAppearance(edit.appearance);
            editSequence->recordUpload(state.appearanceSpare.get(), rows.data(), sizeof(rows),
                                       static_cast<std::uint64_t>(edit.targetIndex) * sizeof(rows));
        }
        editSequence->submit();
        state.project->bindBuffer(3, state.appearanceSpare.get());
        std::swap(state.appearances, state.appearanceSpare);
        return Result<void>::success();
    } catch (const std::exception& error) {
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Failed, error.what(), "physics.field.gpu"));
    }
}

Result<GpuFieldSnapshot> GpuGravitySession::snapshot() const {
    if (!impl_ || impl_->graphicsLifetime.expired())
        return fail<GpuFieldSnapshot>(DiagnosticCode::StaleHandle, "GPU field provider retired");
    try {
        GpuFieldSnapshot snapshot;
        snapshot.stamp = impl_->current;
        snapshot.sources.resize(impl_->sourceCount);
        snapshot.targets.resize(impl_->targetCount);
        impl_->sources[impl_->phase]->downloadBytes(snapshot.sources.data(),
                                                    snapshot.sources.size() * sizeof(GpuSource));
        std::vector<Row> packed(impl_->targetCount * 2);
        impl_->targets[impl_->phase]->downloadBytes(packed.data(), packed.size() * sizeof(Row));
        for (std::size_t i = 0; i < snapshot.targets.size(); ++i)
            for (unsigned axis = 0; axis < 3; ++axis) {
                snapshot.targets[i].position[axis] = packed[i * 2][axis];
                snapshot.targets[i].velocity[axis] = packed[i * 2 + 1][axis];
            }
        return Result<GpuFieldSnapshot>::success(std::move(snapshot));
    } catch (const std::exception& error) {
        return Result<GpuFieldSnapshot>::failure(
            Diagnostic::error(DiagnosticCode::Failed, error.what(), "physics.field.gpu"));
    }
}

FieldStamp GpuGravitySession::stamp() const noexcept { return impl_ ? impl_->current : FieldStamp{}; }

}  // namespace eve::physics::field::gpu
