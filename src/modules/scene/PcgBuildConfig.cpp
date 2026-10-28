#include "scene/PcgBuildConfig.h"
#include <algorithm>
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>
#include "common/SquirrelBinding.h"
#include "scene/PcgGrowth.h"
#include "scene/Scene.h"
#include "scene/SceneNodeRef.h"

// PcgBuildConfig.cpp
namespace eve::scene {
namespace {}
Result<void> PcgBuildConfig::setPublicationType(int type) {
    if (type < 0 || type > 1)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "publication type must be 0 or 1", {}, {}, "scene.pcgBuildConfig"));
    publicationType_ = static_cast<PcgPublicationType>(type);
    return Result<void>::success();
}
Result<void> PcgBuildConfig::addHistory(int category, const std::string& sceneName, int64_t timestamp) {
    if (category < 0 || category > 6)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "build log category must be in range 0..6", {}, {},
                                                       "scene.pcgBuildConfig"));
    history_.push_back({static_cast<PcgBuildLogCategory>(category), sceneName, timestamp});
    return Result<void>::success();
}
Result<int> PcgBuildConfig::getHistoryCategory(uint64_t i) const {
    if (i >= history_.size())
        return Result<int>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "build history index is out of range", {}, {}, "scene.pcgBuildConfig"));
    return Result<int>::success(static_cast<int>(history_[i].category));
}
Result<std::string> PcgBuildConfig::getHistorySceneName(uint64_t i) const {
    if (i >= history_.size())
        return Result<std::string>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "build history index is out of range", {}, {}, "scene.pcgBuildConfig"));
    return Result<std::string>::success(history_[i].sceneName);
}
Result<int64_t> PcgBuildConfig::getHistoryTimestamp(uint64_t i) const {
    if (i >= history_.size())
        return Result<int64_t>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "build history index is out of range", {}, {}, "scene.pcgBuildConfig"));
    return Result<int64_t>::success(history_[i].timestamp);
}
void exposePcgBuildConfigBindings(ssq::Table& t) {
    auto vm = t.getHandle();
    auto c  = t.addClass("PcgBuildConfig", ssq::Class::Ctor<PcgBuildConfig()>());
    c.addFunc("setPublicationType",
              [vm](PcgBuildConfig* s, int v) { return eve::script::projectResult(vm, s->setPublicationType(v)); });
    c.addFunc("getPublicationType", [](const PcgBuildConfig* s) { return s->getPublicationType(); });
    c.addFunc("addHistory", [vm](PcgBuildConfig* s, int category, const std::string& n, int64_t ts) {
        return eve::script::projectResult(vm, s->addHistory(category, n, ts));
    });
    c.addFunc("clearHistory", [](PcgBuildConfig* s) { s->clearHistory(); });
    c.addFunc("getHistoryCount", [](const PcgBuildConfig* s) { return s->getHistoryCount(); });
    c.addFunc("getHistoryCategory", [vm](const PcgBuildConfig* s, uint64_t i) {
        return eve::script::projectResult(vm, s->getHistoryCategory(i), [](int v) { return v; });
    });
    c.addFunc("getHistorySceneName", [vm](const PcgBuildConfig* s, uint64_t i) {
        return eve::script::projectResult(vm, s->getHistorySceneName(i), [](const std::string& v) { return v; });
    });
    c.addFunc("getHistoryTimestamp", [vm](const PcgBuildConfig* s, uint64_t i) {
        return eve::script::projectResult(vm, s->getHistoryTimestamp(i), [](int64_t v) { return v; });
    });
}
}  // namespace eve::scene

// PcgGrowth.cpp
namespace eve::scene {
namespace {
float random01(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return float(x >> 8) * (1.f / 16777216.f);
}
}  // namespace
Result<void> PcgGrowth::configure(float a, float b, float v, float d) {
    if (!std::isfinite(a) || !std::isfinite(b) || !std::isfinite(v) || !std::isfinite(d) || a < .1f || a > 2 ||
        b < .1f || b > 2 || v < 0 || v > 2 || d < .5f || d > 60)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "growth configuration is outside Pcg ranges",
                                                       "scene.pcgGrowth.configure"));
    startScale_ = a;
    endScale_   = b;
    variance_   = v;
    duration_   = d;
    return Result<void>::success();
}
Result<void> PcgGrowth::start(float now, std::uint32_t seed) {
    if (!std::isfinite(now))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "start time must be finite", "scene.pcgGrowth.start"));
    startTime_ = now;
    actualEnd_ = endScale_ + random01(seed) * variance_;
    scale_     = startScale_;
    started_   = true;
    finished_  = false;
    dying_     = false;
    return Result<void>::success();
}
Result<float> PcgGrowth::advance(float now) {
    if (!started_)
        return Result<float>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "growth has not started", "scene.pcgGrowth.advance"));
    if (!std::isfinite(now) || now < startTime_)
        return Result<float>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "time must be finite and monotonic", "scene.pcgGrowth.advance"));
    float t   = std::min((now - startTime_) / duration_, 1.f);
    scale_    = startScale_ + t * (actualEnd_ - startScale_);
    finished_ = t >= 1;
    return Result<float>::success(scale_);
}
Result<void> PcgGrowth::die(float now) {
    if (!std::isfinite(now))
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "death time must be finite", "scene.pcgGrowth.die"));
    deathTime_ = now + 5;
    dying_     = true;
    return Result<void>::success();
}
Result<bool> PcgGrowth::shouldDestroy(float now) const {
    if (!std::isfinite(now))
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "time must be finite", "scene.pcgGrowth.destroy"));
    return Result<bool>::success(dying_ && now >= deathTime_);
}
Result<bool> applyPcgGrowth(PcgGrowth& g, SceneNodeRef& node, float now) {
    auto due = g.shouldDestroy(now);
    if (!due) return Result<bool>::failure(due.status());
    if (due.value()) {
        auto* s = node.getScene();
        if (!s)
            return Result<bool>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                           "growth node cannot be removed", "scene.pcgGrowth.node"));
        auto removed = s->removeLeafAt(node.getHostName(), node.getNodeId());
        if (!removed.ok()) return Result<bool>::failure(removed.status());
        return Result<bool>::success(true);
    }
    auto scale = g.advance(now);
    if (!scale) return Result<bool>::failure(scale.status());
    if (!node.setScale(scale.value(), scale.value(), scale.value()))
        return Result<bool>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument, "growth node cannot be scaled", "scene.pcgGrowth.node"));
    return Result<bool>::success(false);
}
void exposePcgGrowthBindings(ssq::Table& t) {
    auto c  = t.addClass("PcgGrowth", ssq::Class::Ctor<PcgGrowth()>());
    auto vm = t.getHandle();
    c.addFunc("configure", [vm](PcgGrowth* s, float a, float b, float v, float d) {
        return eve::script::projectResult(vm, s->configure(a, b, v, d));
    });
    c.addFunc("start", [vm](PcgGrowth* s, float n, int seed) {
        return eve::script::projectResult(vm, s->start(n, std::uint32_t(seed)));
    });
    c.addFunc("advance", [vm](PcgGrowth* s, float n) {
        return eve::script::projectResult(vm, s->advance(n), [](float v) { return Value(v); });
    });
    c.addFunc("die", [vm](PcgGrowth* s, float n) { return eve::script::projectResult(vm, s->die(n)); });
    c.addFunc("shouldDestroy", [vm](const PcgGrowth* s, float n) {
        return eve::script::projectResult(vm, s->shouldDestroy(n), [](bool v) { return Value(v); });
    });
    c.addFunc("getActualEndScale", [](const PcgGrowth* s) { return s->getActualEndScale(); });
    c.addFunc("getScale", [](const PcgGrowth* s) { return s->getScale(); });
    c.addFunc("getFinished", [](const PcgGrowth* s) { return s->getFinished(); });
    t.addFunc("applyPcgGrowth", [vm](PcgGrowth* g, SceneNodeRef* n, float now) {
        auto r = g && n ? applyPcgGrowth(*g, *n, now)
                        : Result<bool>::failure(Diagnostic::error(
                              DiagnosticCode::InvalidArgument, "growth and node are required", "scene.pcgGrowth.node"));
        return eve::script::projectResult(vm, std::move(r), [](bool v) { return Value(v); });
    });
}
}  // namespace eve::scene
