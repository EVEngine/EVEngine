#pragma once

#include "procgen/PointSet.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/** @brief One named diagnostic step recorded by a script-first generation transaction. */
struct ProcgenStageMetric {
    std::string name;
    int         inputCount   = 0;
    int         outputCount  = 0;
    float       milliseconds = 0.f;
};

/** @brief Staging area for one atomic rebuild of a named procedural system. */
class ProcgenContext {
public:
    ProcgenContext(std::string systemName, uint32_t seed);

    std::string getName() const;
    uint32_t    getSeed() const;
    uint32_t    seedFor(const std::string& scope) const;
    bool        isActive() const;
    bool        hasFailed() const;
    std::string getError() const;

    bool        publish(const std::string& outputName, PointSet* points);
    bool        hasOutput(const std::string& outputName) const;
    int         getOutputCount() const;
    std::string getOutputName(int index) const;
    PointSet*   getOutput(const std::string& outputName) const;

    bool        captureDebug(const std::string& stageName, PointSet* points);
    int         getDebugStageCount() const;
    std::string getDebugStageName(int index) const;
    PointSet*   getDebugStage(const std::string& stageName) const;

    void        trace(const std::string& stageName, int inputCount, int outputCount, float milliseconds);
    int         getTraceCount() const;
    std::string getTraceName(int index) const;
    int         getTraceInputCount(int index) const;
    int         getTraceOutputCount(int index) const;
    float       getTraceMilliseconds(int index) const;

    void fail(const std::string& error);
    void abort();

private:
    friend class Procgen;
    void close();

    std::string                               name_;
    uint32_t                                  seed_   = 1;
    bool                                      active_ = true;
    std::string                               error_;
    std::unordered_map<std::string, PointSet> outputs_;
    std::vector<std::string>                  outputOrder_;
    std::unordered_map<std::string, PointSet> debugStages_;
    std::vector<std::string>                  debugStageOrder_;
    std::vector<ProcgenStageMetric>           traces_;
};

/** @brief Immutable committed snapshot retained by Procgen across script reloads. */
struct ProcgenSystemSnapshot {
    uint32_t                                  seed     = 1;
    uint64_t                                  revision = 0;
    std::unordered_map<std::string, PointSet> outputs;
    std::vector<std::string>                  outputOrder;
    std::unordered_map<std::string, PointSet> debugStages;
    std::vector<std::string>                  debugStageOrder;
    std::vector<ProcgenStageMetric>           traces;
};

}  // namespace eve::procgen
