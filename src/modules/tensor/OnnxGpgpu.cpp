#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <unordered_map>
#include "common/Subscription.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "graphics/Graphics.h"
#include "tensor/OnnxCompiler.h"
#include "tensor/OnnxCompute.h"
namespace eve::tensor {
namespace {
using Clock = std::chrono::steady_clock;
struct Timed {
    double&           milliseconds;
    Clock::time_point start = Clock::now();
    ~Timed() { milliseconds += std::chrono::duration<double, std::milli>(Clock::now() - start).count(); }
};
struct Run;
struct DeviceBytes final : OnnxDeviceStorage {
    std::weak_ptr<Run>                run;
    std::unique_ptr<gpgpu::GpuBuffer> buffer;
    size_t                            size = 0, capacity = 0;
    uint64_t                          epoch = 0;
    ~DeviceBytes() override;
    Result<std::vector<uint8_t>> readback() const override;
};
struct ShaderEntry {
    std::future<onnx_detail::CompiledProgram> compilation;
    std::unique_ptr<gpgpu::ComputeShader>     shader;
};
struct PendingDispatch {
    ShaderEntry*                              program;
    std::vector<std::shared_ptr<DeviceBytes>> bindings;
    uint32_t                                  groups;
};
struct Run : std::enable_shared_from_this<Run> {
    gpgpu::Gpgpu&                                gp;
    std::unique_ptr<gpgpu::Sequence>             sequence;
    std::unordered_map<std::string, ShaderEntry> shaders;
    onnx_detail::CompilerQueue                   compiler;
    std::vector<PendingDispatch>                 dispatches;
    double                                       compileTaskMs = 0, compileWaitMs = 0, pipelineMs = 0;
    std::map<std::shared_ptr<const std::vector<uint8_t>>, std::shared_ptr<DeviceBytes>,
             std::owner_less<std::shared_ptr<const std::vector<uint8_t>>>>
                                                             uploads, persistentUploads;
    std::vector<std::shared_ptr<const std::vector<uint8_t>>> uncommittedUploads;
    bool                                                     active = true;
    uint64_t                                                 epoch  = 1;
    std::vector<std::shared_ptr<DeviceBytes>>                pending;
    std::vector<std::weak_ptr<DeviceBytes>>                  resources;
    OnnxTransferStats                                        stats;
    double shaderMs = 0, allocationMs = 0, uploadRecordMs = 0, dispatchRecordMs = 0, waitMs = 0, readbackMs = 0;
    size_t compiledShaders                                           = 0;
    Clock::time_point                                        started = Clock::now();
    size_t                                                   queued = 0, cachedBytes = 0, allocatedBytes = 0;
    std::multimap<size_t, std::unique_ptr<gpgpu::GpuBuffer>> freeBuffers;
    explicit Run(gpgpu::Gpgpu& g, uint32_t workers) : gp(g), sequence(g.newSequence()), compiler(workers) {
        sequence->begin();
    }
    ~Run() {
        // Destroy an unsubmitted command sequence before its referenced resources.
        sequence.reset();
        for (auto& [key, entry] : shaders)
            if (entry.shader) entry.shader->clearBindings();
        for (auto& weak : resources)
            if (auto p = weak.lock()) p->buffer.reset();
    }
    void beginCall() noexcept {
        stats = {};
        compiler.resetPeak();
        compileTaskMs = compileWaitMs = pipelineMs = 0;
        shaderMs = allocationMs = uploadRecordMs = dispatchRecordMs = waitMs = readbackMs = 0;
        compiledShaders                                                                   = 0;
        started                                                                           = Clock::now();
        active                                                                            = true;
        ++epoch;
    }
    void endCall() noexcept {
        compiler.waitIdle();
        // An abandoned graph must not record or submit its deferred commands.
        // Unmaterialized jobs contain only CPU data; discard their future (including any
        // compilation exception) because the caller already received the graph failure.
        dispatches.clear();
        std::erase_if(shaders, [](const auto& item) { return !item.second.shader; });
        if (const auto* profile = std::getenv("EVE_ONNX_PROFILE"); profile && std::string(profile) == "1")
            std::cerr << "ONNX_PROFILE shader_ms=" << shaderMs << " allocation_ms=" << allocationMs
                      << " upload_record_ms=" << uploadRecordMs << " dispatch_record_ms=" << dispatchRecordMs
                      << " submit_wait_ms=" << waitMs << " readback_host_ms=" << readbackMs
                      << " compile_task_ms=" << compileTaskMs << " compile_wait_ms=" << compileWaitMs
                      << " pipeline_ms=" << pipelineMs << " compiler_peak_workers=" << compiler.peakWorkers()
                      << " compiled_shaders=" << compiledShaders << " resident_scope_ms="
                      << std::chrono::duration<double, std::milli>(Clock::now() - started).count() << '\n';
        sequence.reset();
        for (auto& [key, entry] : shaders)
            if (entry.shader) entry.shader->clearBindings();
        // Abandoned recorded uploads have not reached the device and cannot enter a persistent cache.
        for (const auto& key : uncommittedUploads) {
            if (auto it = persistentUploads.find(key); it != persistentUploads.end()) {
                cachedBytes -= key->size();
                persistentUploads.erase(it);
            }
        }
        uncommittedUploads.clear();
        pending.clear();
        uploads.clear();
        queued = 0;
        active = false;
        std::erase_if(resources, [](const auto& p) { return p.expired(); });
    }
    std::shared_ptr<DeviceBytes> allocate(size_t bytes) {
        Timed timing{allocationMs};
        if (!bytes || bytes > INT32_MAX - 3) throw std::runtime_error("Invalid resident buffer extent");
        auto p              = std::make_shared<DeviceBytes>();
        p->run              = shared_from_this();
        p->size             = bytes;
        p->epoch            = epoch;
        const auto capacity = std::max(size_t(4), (bytes + 3) & ~size_t(3));
        auto       reusable = freeBuffers.lower_bound(capacity);
        if (reusable != freeBuffers.end() && reusable->first <= std::max(size_t(4096), capacity * 2)) {
            p->capacity = reusable->first;
            p->buffer   = std::move(reusable->second);
            freeBuffers.erase(reusable);
            ++stats.bufferReuses;
        } else {
            if (allocatedBytes + capacity > 512u * 1024u * 1024u) {
                for (const auto& [n, b] : freeBuffers) allocatedBytes -= n;
                freeBuffers.clear();
            }
            if (allocatedBytes + capacity > 512u * 1024u * 1024u)
                throw std::runtime_error("ONNX GPU resident memory limit exceeded");
            p->buffer.reset(gp.newBuffer(static_cast<int>(capacity)));
            p->capacity = capacity;
            allocatedBytes += capacity;
            ++stats.bufferAllocations;
        }
        resources.push_back(p);
        return p;
    }
    void recordDispatches() {
        for (auto& command : dispatches) {
            auto& entry = *command.program;
            if (!entry.shader) {
                Timed                        shaderTiming{shaderMs};
                onnx_detail::CompiledProgram program;
                {
                    Timed waitTiming{compileWaitMs};
                    program = entry.compilation.get();
                }
                compileTaskMs += program.milliseconds;
                {
                    Timed pipelineTiming{pipelineMs};
                    auto  result = gpgpu::createComputeShader(program.words);
                    if (!result.ok()) throw std::runtime_error(result.error()->message());
                    entry.shader = std::move(result.value());
                }
            }
            Timed timing{dispatchRecordMs};
            auto& shader = *entry.shader;
            for (size_t i = 0; i < command.bindings.size(); ++i)
                shader.bindBuffer(static_cast<int>(i), command.bindings[i]->buffer.get());
            sequence->recordDispatch(&shader, static_cast<int>(command.groups));
            shader.clearBindings();
        }
        stats.compilerPeakWorkers = compiler.peakWorkers();
        dispatches.clear();
    }
    void flush() {
        if (!queued) return;
        recordDispatches();
        Timed timing{waitMs};
        // Submit asynchronously through the existing sequence contract; CPU visibility is
        // requested only at a graph boundary. Retain every referenced allocation until wait.
        if (sequence->submitAsync() != gpgpu::SequenceStatus::Submitted ||
            sequence->wait() != gpgpu::SequenceStatus::Complete)
            throw std::runtime_error("ONNX GPU submission failed");
        uncommittedUploads.clear();
        ++stats.submissions;
        pending.clear();
        queued = 0;
        sequence->begin();
    }
    std::shared_ptr<DeviceBytes> input(const OnnxBuffer& value) {
        if (value.device) {
            auto d = std::dynamic_pointer_cast<DeviceBytes>(value.device);
            if (!d || d->run.lock().get() != this || !d->buffer || d->epoch != epoch || !active)
                throw std::runtime_error("Foreign or expired GPU storage");
            return d;
        }
        if (!value.host || value.host->size() != value.size) throw std::runtime_error("Invalid host input storage");
        auto& cache = value.persistent ? persistentUploads : uploads;
        if (auto it = cache.find(value.host); it != cache.end()) {
            it->second->epoch = epoch;
            return it->second;
        }
        if (value.persistent && cachedBytes + value.size > 128u * 1024u * 1024u) {
            persistentUploads.clear();
            cachedBytes = 0;
        }
        auto                 d = allocate(std::max(size_t(1), value.size));
        Timed                timing{uploadRecordMs};
        std::vector<uint8_t> padded(std::max(size_t(4), (value.size + 3) & ~size_t(3)), 0);
        std::copy(value.host->begin(), value.host->end(), padded.begin());
        sequence->recordUpload(d->buffer.get(), padded.data(), padded.size());
        ++stats.uploads;
        stats.uploadedBytes += value.size;
        ++queued;
        cache.emplace(value.host, d);
        if (value.persistent) {
            cachedBytes += value.size;
            uncommittedUploads.push_back(value.host);
        }
        pending.push_back(d);
        return d;
    }
};
DeviceBytes::~DeviceBytes() {
    if (buffer)
        if (auto owner = run.lock()) owner->freeBuffers.emplace(capacity, std::move(buffer));
}
Result<std::vector<uint8_t>> DeviceBytes::readback() const {
    try {
        auto owner = run.lock();
        if (!owner || !buffer || !owner->active || epoch != owner->epoch)
            throw std::runtime_error("ONNX GPU storage expired");
        // Dispatches must precede the download in the command stream. Compilation
        // waits are accounted separately from host readback and GPU submission.
        owner->recordDispatches();
        const auto                        start     = Clock::now();
        const auto                        priorWait = owner->waitMs;
        std::unique_ptr<gpgpu::GpuBuffer> staging(owner->gp.newBuffer(static_cast<int>(size), "staging"));
        owner->sequence->recordDownload(buffer.get(), staging.get(), size);
        ++owner->queued;
        owner->flush();
        std::vector<uint8_t> out(size);
        staging->downloadBytes(out.data(), size);
        owner->readbackMs +=
            std::chrono::duration<double, std::milli>(Clock::now() - start).count() - (owner->waitMs - priorWait);
        ++owner->stats.downloads;
        owner->stats.downloadedBytes += size;
        return Result<std::vector<uint8_t>>::success(std::move(out));
    } catch (const std::exception& e) {
        return Result<std::vector<uint8_t>>::failure(Diagnostic::error(DiagnosticCode::Failed, e.what()));
    }
}
struct SessionState {
    std::shared_ptr<Run> run;
    bool                 retired         = false;
    uint32_t             compilerWorkers = 4;
};
class EngineCompute final : public OnnxCompute {
    std::shared_ptr<SessionState> state;
    eve::Subscription             retirement;
    OnnxTransferStats             last;
    bool                          active = false;

protected:
    void beginRun() noexcept override {
        if (state->run) state->run->beginCall();
        last   = {};
        active = true;
    }
    void endRun() noexcept override {
        if (state->run) {
            last = state->run->stats;
            state->run->endCall();
        }
        active = false;
    }

public:
    EngineCompute(std::shared_ptr<SessionState> s, eve::Subscription token)
        : state(std::move(s)), retirement(std::move(token)) {}
    OnnxTransferStats  transferStats() const override { return active && state->run ? state->run->stats : last; }
    Result<OnnxBuffer> enqueue(const std::string& source, std::span<const OnnxBuffer> inputs, size_t bytes,
                               uint32_t work) override {
        try {
            auto& run = state->run;
            if (state->retired) throw std::runtime_error("ONNX GPU session device has been retired");
            if (!active) throw std::runtime_error("Resident enqueue requires an active runGpu scope");
            if (inputs.size() >= 8 || !bytes || !work || (uint64_t(work) + 63) / 64 > 65535)
                throw std::runtime_error("Invalid GPU kernel extent");
            if (!run) {
                auto* gp = gpgpu::Gpgpu::create();
                if (!gp || !gp->isAvailable()) throw std::runtime_error("ONNX GPU device unavailable");
                run = std::make_shared<Run>(*gp, state->compilerWorkers);
            }
            if (!run->sequence) {
                run->sequence.reset(run->gp.newSequence());
                run->sequence->begin();
            }
            // Bound pending resources and the existing shader descriptor pool (64 sets).
            if (run->queued >= 48) run->flush();
            auto it = run->shaders.find(source);
            if (it == run->shaders.end()) {
                if (run->shaders.size() >= 1024) {
                    run->flush();
                    run->shaders.clear();
                }
                ++run->stats.shaderCompilations;
                ++run->compiledShaders;
                it = run->shaders.emplace(source, ShaderEntry{run->compiler.enqueue(source), {}}).first;
            } else
                ++run->stats.shaderCacheHits;
            PendingDispatch command{&it->second, {}, (work + 63) / 64};
            for (const auto& input : inputs) {
                auto d = run->input(input);
                command.bindings.push_back(d);
                run->pending.push_back(d);
            }
            auto out = run->allocate(bytes);
            command.bindings.push_back(out);
            run->dispatches.push_back(std::move(command));
            run->pending.push_back(out);
            ++run->queued;
            return Result<OnnxBuffer>::success({{}, out, bytes});
        } catch (const std::exception& e) {
            return Result<OnnxBuffer>::failure(
                Diagnostic::error(DiagnosticCode::Failed, e.what(), {}, {}, "tensor.onnx.gpu"));
        }
    }
    Result<std::vector<uint8_t>> dispatch(const OnnxKernel& k) override {
        const bool temporary = !active;
        if (temporary) beginRun();
        struct Scope {
            EngineCompute& owner;
            bool           temporary;
            ~Scope() {
                if (temporary) owner.endRun();
            }
        } scope{*this, temporary};
        std::vector<OnnxBuffer> inputs;
        for (auto s : k.inputs) {
            auto p = std::make_shared<const std::vector<uint8_t>>(s.begin(), s.end());
            inputs.push_back({p, {}, p->size()});
        }
        auto r = enqueue(k.source, inputs, k.outputBytes, k.workItems);
        if (!r.ok()) return Result<std::vector<uint8_t>>::failure(r.status());
        return r.value().device->readback();
    }
};
}  // namespace
Result<std::unique_ptr<OnnxCompute>> createOnnxGpuCompute(uint32_t compilerWorkers) {
    if (compilerWorkers < 1 || compilerWorkers > 8)
        return Result<std::unique_ptr<OnnxCompute>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "Compiler worker count must be 1..8"));
#ifdef EVENGINE_WEBGPU
    return Result<std::unique_ptr<OnnxCompute>>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "ONNX GLSL kernels require the Vulkan backend"));
#else
    try {
        auto* gp = gpgpu::Gpgpu::create();
        if (gp && gp->isAvailable()) {
            auto state                        = std::make_shared<SessionState>();
            state->compilerWorkers            = compilerWorkers;
            std::weak_ptr<SessionState> weak  = state;
            auto                        token = graphics::Graphics::create()->onResourcesRetiring([weak]() noexcept {
                if (auto s = weak.lock()) {
                    s->retired = true;
                    s->run.reset();
                }
            });
            if (!token.ok()) return Result<std::unique_ptr<OnnxCompute>>::failure(token.status());
            return Result<std::unique_ptr<OnnxCompute>>::success(
                std::make_unique<EngineCompute>(state, std::move(token.value())));
        }
    } catch (const std::exception& e) {
        return Result<std::unique_ptr<OnnxCompute>>::failure(Diagnostic::error(DiagnosticCode::Failed, e.what()));
    }
    return Result<std::unique_ptr<OnnxCompute>>::failure(Diagnostic::error(
        DiagnosticCode::Unsupported, "Initialize the engine Vulkan graphics device before ONNX GPU execution"));
#endif
}
}  // namespace eve::tensor
