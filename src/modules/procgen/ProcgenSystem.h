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

/** @brief One committed PointSet cache entry for a named script pipeline stage. */
struct ProcgenCachedStage {
    std::string cacheKey;
    PointSet    points;
};

/** @brief One currently running automatic stage timer. */
struct ProcgenOpenTrace {
    std::string name;
    int         inputCount         = 0;
    uint64_t    startedNanoseconds = 0;
};

/** @brief Staging area for one atomic rebuild of a named procedural system. */
class EVENGINE_API_DOMAINS ProcgenContext {
public:
    /** @brief Procgen context. */
    ProcgenContext(std::string systemName, uint32_t seed, std::string buildKey = {}, bool cacheHit = false);

    /** @brief Returns the name. */
    std::string getName() const;
    /** @brief Returns the seed. */
    uint32_t    getSeed() const;
    /** @brief Seed for. */
    uint32_t    seedFor(const std::string& scope) const;
    /** @brief True when active. */
    bool        isActive() const;
    /** @brief True when failed. */
    bool        hasFailed() const;
    /** @brief True when cache hit. */
    bool        isCacheHit() const;
    /** @brief Returns the error. */
    std::string getError() const;
    /** @brief Returns the build key. */
    std::string getBuildKey() const;

    /** @brief Publish. */
    bool        publish(const std::string& outputName, PointSet* points);
    /** @brief True when output. */
    bool        hasOutput(const std::string& outputName) const;
    /** @brief Returns the output count. */
    int         getOutputCount() const;
    /** @brief Returns the output name. */
    std::string getOutputName(int index) const;
    /** @brief Returns the output. */
    PointSet*   getOutput(const std::string& outputName) const;

    /** @brief Capture debug. */
    bool        captureDebug(const std::string& stageName, PointSet* points);
    /** @brief Returns the debug stage count. */
    int         getDebugStageCount() const;
    /** @brief Returns the debug stage name. */
    std::string getDebugStageName(int index) const;
    /** @brief Returns the debug stage. */
    PointSet*   getDebugStage(const std::string& stageName) const;

    /** @brief Reuse stage. */
    PointSet* reuseStage(const std::string& stageName, const std::string& cacheKey);
    /** @brief Cache stage. */
    bool      cacheStage(const std::string& stageName, const std::string& cacheKey, PointSet* points);
    /** @brief Returns the stage cache hit count. */
    int       getStageCacheHitCount() const;
    /** @brief Returns the stage cache miss count. */
    int       getStageCacheMissCount() const;

    /** @brief Trace. */
    void        trace(const std::string& stageName, int inputCount, int outputCount, float milliseconds);
    /** @brief Start an automatically timed diagnostic stage. Timers may be nested. */
    bool        beginTrace(const std::string& stageName, int inputCount);
    /** @brief Finish the most recently started timer and append its measured trace. */
    bool        endTrace(int outputCount);
    /** @brief Number of automatic timers that have not yet been finished. */
    int         getOpenTraceCount() const;
    /** @brief Returns the trace count. */
    int         getTraceCount() const;
    /** @brief Returns the trace name. */
    std::string getTraceName(int index) const;
    /** @brief Returns the trace input count. */
    int         getTraceInputCount(int index) const;
    /** @brief Returns the trace output count. */
    int         getTraceOutputCount(int index) const;
    /** @brief Returns the trace milliseconds. */
    float       getTraceMilliseconds(int index) const;

    /** @brief Fail. */
    void fail(const std::string& error);
    /** @brief Aborts . */
    void abort();

private:
    friend class Procgen;
    void close();

    std::string                                         name_;
    uint32_t                                            seed_     = 1;
    bool                                                active_   = true;
    bool                                                cacheHit_ = false;
    std::string                                         error_;
    std::string                                         buildKey_;
    std::unordered_map<std::string, PointSet>           outputs_;
    std::vector<std::string>                            outputOrder_;
    std::unordered_map<std::string, PointSet>           debugStages_;
    std::vector<std::string>                            debugStageOrder_;
    std::unordered_map<std::string, ProcgenCachedStage> stageCache_;
    int                                                 stageCacheHits_   = 0;
    int                                                 stageCacheMisses_ = 0;
    std::vector<ProcgenStageMetric>                     traces_;
    std::vector<ProcgenOpenTrace>                       openTraces_;
};

/** @brief Immutable committed snapshot retained by Procgen across script reloads. */
struct ProcgenSystemSnapshot {
    uint32_t                                            seed     = 1;
    uint64_t                                            revision = 0;
    std::string                                         buildKey;
    std::unordered_map<std::string, PointSet>           outputs;
    std::vector<std::string>                            outputOrder;
    std::unordered_map<std::string, PointSet>           debugStages;
    std::vector<std::string>                            debugStageOrder;
    std::unordered_map<std::string, ProcgenCachedStage> stageCache;
    int                                                 stageCacheHits   = 0;
    int                                                 stageCacheMisses = 0;
    std::vector<ProcgenStageMetric>                     traces;
};

}  // namespace eve::procgen
