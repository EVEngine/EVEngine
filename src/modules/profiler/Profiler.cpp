#include "profiler/Profiler.h"

#include "common/GpuTimer.h"
#include "common/Capability.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <chrono>
#include <cstdint>

namespace eve::profiler {

Module_IMPL(Profiler, new Profiler());

namespace {
int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}  // namespace

Profiler::Profiler() {
    eve::debug::setRenderTracer(this);
    registerProfilerCapabilities();
}

Profiler::~Profiler() {
    if (eve::debug::renderTracer() == this) eve::debug::setRenderTracer(nullptr);
}

void Profiler::setEnabled(bool on) {
    if (!on || !enabled()) frameBeginNs_ = 0;
    eve::prof::Profiler::setEnabled(on);
    if (!on) {
        frameMs_ = 0.f;
    }
}

bool Profiler::enabled() const { return eve::prof::Profiler::enabled(); }

const char* Profiler::internScopeName(const std::string& name) {
    // unordered_set nodes are stable across rehash, so c_str() stays valid for the
    // module's lifetime; distinct names are bounded by what a game's scripts use.
    const auto inserted = scopeNames_.insert(name.empty() ? std::string("scope") : name);
    return inserted.first->c_str();
}

void Profiler::reset() {
    eve::prof::Profiler::reset();
    frameMs_ = 0.f;
    captureSequence_ = 0;
    frameBeginNs_    = 0;
}

void Profiler::ensureGpuTimer() const {
    // Retry only while unbound so a late graphics provider can still attach; once
    // resolved (including to nullptr after a successful provide/revoke cycle is
    // not re-probed from hot paths — callers rebind by constructing a new module).
    if (!gpuTimer_) gpuTimer_ = eve::cap::ProviderRef<eve::service::IGpuTimer>::bind();
}

void Profiler::beginFrame() {
    frameBeginNs_ = nowNs();
    ensureGpuTimer();
}

void Profiler::endFrame() {
    if (!enabled() || frameBeginNs_ == 0) return;
    const double totalMs = static_cast<double>(nowNs() - frameBeginNs_) / 1'000'000.0;
    frameMs_             = static_cast<float>(totalMs);
    eve::prof::Profiler::frameMark();
    ++captureSequence_;
    frameBeginNs_ = 0;
}

void Profiler::begin(const char* name) {
    if (!enabled()) return;
    eve::prof::Profiler::zoneBegin(name ? name : "scope", nullptr);
}

void Profiler::end() {
    if (!enabled()) return;
    eve::prof::Profiler::zoneEnd();
}

void Profiler::frameBegin() {
    if (enabled() && frameBeginNs_ == 0) beginFrame();
}

void Profiler::frameEnd() { endFrame(); }

void Profiler::passBegin(const char* name) {
    if (!enabled()) return;
    eve::prof::Profiler::zoneBegin(name ? name : "pass", "graphics");
}

void Profiler::passEnd(const char* name) {
    if (!enabled()) return;
    eve::prof::Profiler::zoneEnd();
}

bool Profiler::hasFrame() const { return eve::prof::Profiler::hasFrame(); }

float Profiler::frameMs() const { return frameMs_; }

float Profiler::gpuFrameMs() const {
    ensureGpuTimer();
    return gpuTimer_ ? gpuTimer_->gpuFrameMs() : 0.f;
}

eve::Result<ProfilerFrameSnapshot> Profiler::captureFrame() const {
    if (!hasFrame()) {
        return eve::Result<ProfilerFrameSnapshot>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "No completed profiler frame is available",
            "profiler.frame", {}, "profiler.capture-frame"));
    }
    ProfilerFrameSnapshot snapshot;
    snapshot.sequence   = captureSequence_;
    snapshot.cpuFrameMs = frameMs_;
    ensureGpuTimer();
    if (gpuTimer_) {
        snapshot.gpuTimingAvailable = gpuTimer_->gpuTimingAvailable();
        if (snapshot.gpuTimingAvailable) snapshot.gpuFrameMs = gpuTimer_->gpuFrameMs();
    }
    const auto& samples = eve::prof::Profiler::lastFrame();
    snapshot.zones.reserve(samples.size());
    for (const auto& sample : samples) {
        snapshot.zones.push_back({sample.module, sample.name, sample.thread, sample.selfMs,
                                  sample.totalMs, sample.count, sample.minDepth});
    }
    return eve::Result<ProfilerFrameSnapshot>::success(std::move(snapshot));
}

std::string Profiler::textReport() const {
    std::string report = eve::prof::Profiler::textReport();
    ensureGpuTimer();
    if (gpuTimer_ && gpuTimer_->gpuTimingAvailable()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "  GPU graphics-queue span (delayed): %.2f ms\n", gpuTimer_->gpuFrameMs());
        report += buf;
    }
    return report;
}

ssq::Object Profiler::capture() {
    ssq::Array out(vm_);
    for (const auto& s : eve::prof::Profiler::lastFrame()) {
        ssq::Table t(vm_);
        t.set("module", s.module);
        t.set("name", s.name);
        t.set("thread", s.thread);
        t.set("selfMs", static_cast<float>(s.selfMs));
        t.set("totalMs", static_cast<float>(s.totalMs));
        t.set("count", s.count);
        out.push(t);
    }
    return out;
}

void Profiler::expose(ssq::Table& table) {
    if (Profiler* self = Profiler::create()) self->vm_ = table.getHandle();
    auto cls = table.addClass(name, Profiler::create, false);
    expose(cls);
}

void Profiler::expose(ssq::Class& cls) {
    cls.addFunc("setEnabled", &Profiler::setEnabled);
    cls.addFunc("enabled", &Profiler::enabled);
    cls.addFunc("reset", &Profiler::reset);
    cls.addFunc("beginFrame", &Profiler::beginFrame);
    cls.addFunc("endFrame", &Profiler::endFrame);
    // Two shapes have to be bridged here, both discovered by the script smoke test.
    // `&Profiler::begin` takes `const char*`, which the binding layer treats as an
    // opaque pointer type and expects an *instance* for, so a script passing a string
    // failed with "bad cast expected: INSTANCE got: STRING". Converting to std::string
    // fixes that, but the collector keeps the pointer and reads it at frameMark(), so
    // the string must also be interned rather than passed as a temporary's c_str().
    cls.addFunc("begin", [](Profiler* self, const std::string& name) {
        if (self) self->begin(self->internScopeName(name));
    });
    cls.addFunc("end", &Profiler::end);
    cls.addFunc("hasFrame", &Profiler::hasFrame);
    cls.addFunc("frameMs", &Profiler::frameMs);
    cls.addFunc("gpuFrameMs", &Profiler::gpuFrameMs);
    cls.addFunc("textReport", &Profiler::textReport);
    cls.addFunc("capture", &Profiler::capture);
}

}  // namespace eve::profiler
