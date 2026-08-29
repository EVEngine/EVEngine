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

Profiler::Profiler() { eve::debug::setRenderTracer(this); }

Profiler::~Profiler() {
    if (eve::debug::renderTracer() == this) eve::debug::setRenderTracer(nullptr);
}

void Profiler::setEnabled(bool on) {
    eve::prof::Profiler::setEnabled(on);
    if (!on) {
        frameMs_ = 0.f;
    }
}

bool Profiler::enabled() const { return eve::prof::Profiler::enabled(); }

void Profiler::reset() {
    eve::prof::Profiler::reset();
    frameMs_ = 0.f;
}

void Profiler::beginFrame() { frameBeginNs_ = nowNs(); }

void Profiler::endFrame() {
    if (!enabled()) return;
    const double totalMs = static_cast<double>(nowNs() - frameBeginNs_) / 1'000'000.0;
    frameMs_             = static_cast<float>(totalMs);
    eve::prof::Profiler::frameMark();
}

void Profiler::begin(const char* name) {
    if (!enabled()) return;
    eve::prof::Profiler::zoneBegin(name ? name : "scope", nullptr);
}

void Profiler::end() {
    if (!enabled()) return;
    eve::prof::Profiler::zoneEnd();
}

void Profiler::frameBegin() { beginFrame(); }

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
    auto* t = eve::cap::query<eve::service::IGpuTimer>();
    return t ? t->gpuFrameMs() : 0.f;
}

std::string Profiler::textReport() const {
    std::string report = eve::prof::Profiler::textReport();
    auto* t            = eve::cap::query<eve::service::IGpuTimer>();
    if (t && t->gpuTimingAvailable()) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "  GPU frame: %.2f ms\n", t->gpuFrameMs());
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
    cls.addFunc("begin", &Profiler::begin);
    cls.addFunc("end", &Profiler::end);
    cls.addFunc("hasFrame", &Profiler::hasFrame);
    cls.addFunc("frameMs", &Profiler::frameMs);
    cls.addFunc("gpuFrameMs", &Profiler::gpuFrameMs);
    cls.addFunc("textReport", &Profiler::textReport);
    cls.addFunc("capture", &Profiler::capture);
}

}  // namespace eve::profiler
