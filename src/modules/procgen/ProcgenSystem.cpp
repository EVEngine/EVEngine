#include "procgen/ProcgenSystem.h"

#include <algorithm>
#include <utility>

namespace eve::procgen {

ProcgenContext::ProcgenContext(std::string systemName, uint32_t seed, std::string buildKey, bool cacheHit)
    : name_(std::move(systemName)),
      seed_(seed ? seed : 1u),
      active_(!cacheHit),
      cacheHit_(cacheHit),
      buildKey_(std::move(buildKey)) {}

std::string ProcgenContext::getName() const { return name_; }
uint32_t    ProcgenContext::getSeed() const { return seed_; }
uint32_t    ProcgenContext::seedFor(const std::string& scope) const { return deriveSeed(seed_, scope); }
bool        ProcgenContext::isActive() const { return active_; }
bool        ProcgenContext::hasFailed() const { return !error_.empty(); }
bool        ProcgenContext::isCacheHit() const { return cacheHit_; }
std::string ProcgenContext::getError() const { return error_; }
std::string ProcgenContext::getBuildKey() const { return buildKey_; }

bool ProcgenContext::publish(const std::string& outputName, PointSet* points) {
    if (!active_) {
        error_ = "publish: transaction is closed";
        return false;
    }
    if (outputName.empty()) {
        error_ = "publish: output name is empty";
        return false;
    }
    if (!points) {
        error_ = "publish: null PointSet";
        return false;
    }
    if (outputs_.find(outputName) == outputs_.end()) outputOrder_.push_back(outputName);
    outputs_[outputName] = *points;
    return true;
}

bool ProcgenContext::hasOutput(const std::string& outputName) const {
    return outputs_.find(outputName) != outputs_.end();
}

int ProcgenContext::getOutputCount() const { return int(outputOrder_.size()); }

std::string ProcgenContext::getOutputName(int index) const {
    return index >= 0 && index < int(outputOrder_.size()) ? outputOrder_[size_t(index)] : std::string();
}

PointSet* ProcgenContext::getOutput(const std::string& outputName) const {
    const auto found = outputs_.find(outputName);
    return found == outputs_.end() ? nullptr : new PointSet(found->second);
}

bool ProcgenContext::captureDebug(const std::string& stageName, PointSet* points) {
    if (!active_) {
        error_ = "captureDebug: transaction is closed";
        return false;
    }
    if (stageName.empty()) {
        error_ = "captureDebug: stage name is empty";
        return false;
    }
    if (!points) {
        error_ = "captureDebug: null PointSet";
        return false;
    }
    if (debugStages_.find(stageName) == debugStages_.end()) debugStageOrder_.push_back(stageName);
    debugStages_[stageName] = *points;
    return true;
}

int ProcgenContext::getDebugStageCount() const { return int(debugStageOrder_.size()); }

std::string ProcgenContext::getDebugStageName(int index) const {
    return index >= 0 && index < int(debugStageOrder_.size()) ? debugStageOrder_[size_t(index)] : std::string();
}

PointSet* ProcgenContext::getDebugStage(const std::string& stageName) const {
    const auto found = debugStages_.find(stageName);
    return found == debugStages_.end() ? nullptr : new PointSet(found->second);
}

PointSet* ProcgenContext::reuseStage(const std::string& stageName, const std::string& cacheKey) {
    if (!active_ || stageName.empty() || cacheKey.empty()) return nullptr;
    const auto found = stageCache_.find(stageName);
    if (found == stageCache_.end() || found->second.cacheKey != cacheKey) {
        ++stageCacheMisses_;
        return nullptr;
    }
    ++stageCacheHits_;
    return new PointSet(found->second.points);
}

bool ProcgenContext::cacheStage(const std::string& stageName, const std::string& cacheKey, PointSet* points) {
    if (!active_) {
        error_ = "cacheStage: transaction is closed";
        return false;
    }
    if (stageName.empty() || cacheKey.empty()) {
        error_ = "cacheStage: stage name and cache key are required";
        return false;
    }
    if (!points) {
        error_ = "cacheStage: null PointSet";
        return false;
    }
    stageCache_[stageName] = {cacheKey, *points};
    return true;
}

int ProcgenContext::getStageCacheHitCount() const { return stageCacheHits_; }
int ProcgenContext::getStageCacheMissCount() const { return stageCacheMisses_; }

void ProcgenContext::trace(const std::string& stageName, int inputCount, int outputCount, float milliseconds) {
    if (!active_ || stageName.empty()) return;
    traces_.push_back({stageName, std::max(0, inputCount), std::max(0, outputCount), std::max(0.f, milliseconds)});
}

int ProcgenContext::getTraceCount() const { return int(traces_.size()); }

std::string ProcgenContext::getTraceName(int index) const {
    return index >= 0 && index < int(traces_.size()) ? traces_[size_t(index)].name : std::string();
}

int ProcgenContext::getTraceInputCount(int index) const {
    return index >= 0 && index < int(traces_.size()) ? traces_[size_t(index)].inputCount : 0;
}

int ProcgenContext::getTraceOutputCount(int index) const {
    return index >= 0 && index < int(traces_.size()) ? traces_[size_t(index)].outputCount : 0;
}

float ProcgenContext::getTraceMilliseconds(int index) const {
    return index >= 0 && index < int(traces_.size()) ? traces_[size_t(index)].milliseconds : 0.f;
}

void ProcgenContext::fail(const std::string& error) {
    if (active_) error_ = error.empty() ? "generation failed" : error;
}

void ProcgenContext::abort() {
    if (!active_) return;
    error_ = error_.empty() ? "transaction aborted" : error_;
    close();
}

void ProcgenContext::close() { active_ = false; }

}  // namespace eve::procgen
