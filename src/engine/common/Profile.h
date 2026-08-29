#pragma once

#ifndef EVENGINE_COMMON_PROFILE_H
#define EVENGINE_COMMON_PROFILE_H

#include "common/Export.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Engine-wide profiling core (Tracy / UE Insights style scoped zones).
//
// Any module may sprinkle scoped zones at its per-frame entry points:
//
//     void World::update(float dt) {
//         EV_PROFILE_MODULE("physics", "World::update");
//         ...
//     }
//
// Zones are cheap when disabled (a single atomic branch); when enabled they
// push {name, module, thread, start, end, parent} into a per-thread buffer and
// frameMark() aggregates the frame into a call tree with self/total time per
// module and per zone. This core lives in common/ so lower layers can use it
// without violating the module layering rules.
// ---------------------------------------------------------------------------

namespace eve::prof {

/**
 * @brief One completed profiling zone (pre-order, parent-linked within a thread).
 */
struct EVENGINE_API ZoneRecord {
    const char* name      = nullptr;   // pointer is static storage, never freed
    uint32_t    module    = 0;         // interned module id (0 = default)
    uint32_t    threadId  = 0;
    int32_t     parent    = -1;        // index into the same thread's buffer
    int16_t     depth     = 0;
    int64_t     startNs   = 0;
    int64_t     endNs     = 0;         // 0 while open
    uint8_t     kind      = 0;         // 0 = CPU, 1 = GPU
};

/**
 * @brief Per-module/per-zone aggregate of the last completed frame.
 */
struct EVENGINE_API ZoneSample {
    std::string module;
    std::string name;
    std::string thread;
    double      selfMs   = 0.0;
    double      totalMs  = 0.0;
    int         count    = 0;
    int         minDepth = 0x7fffffff;
};

/**
 * @brief Global zone collector + per-frame aggregator.
 *
 * All methods are cheap when disabled. Most methods are called from the main
 * thread; the collector also snapshots off-main-thread buffers at frameMark().
 */
class EVENGINE_API Profiler {
public:
    /** @brief Turns collection on/off globally (off = single-branch no-op). */
    static void setEnabled(bool on);
    /** @brief True when collection is active. */
    static bool enabled();

    /** @brief Opens a scoped zone on the current thread. */
    static void zoneBegin(const char* name, const char* module = nullptr);
    /** @brief Closes the innermost open zone on the current thread. */
    static void zoneEnd();

    /**
     * @brief Finalizes the current frame: snapshots all thread buffers and
     *        aggregates them into lastFrame().
     * @return Wall time spent aggregating (not the frame duration).
     */
    static double frameMark();

    /** @brief True once at least one frame has been aggregated. */
    static bool hasFrame();
    /** @brief Per-module/per-zone samples of the last frame, sorted by self time. */
    static const std::vector<ZoneSample>& lastFrame();
    /** @brief Human-readable report of the last frame. */
    static std::string textReport();

    /** @brief Clears all collected state. */
    static void reset();

    /** @brief Interns a module name and returns its stable id. */
    static uint32_t moduleId(const char* module);

private:
    struct ThreadBuffer;
    struct Frame;

    static ThreadBuffer& currentBuffer();

    /** @brief Registry of live thread buffers (locked internally). */
    static std::vector<ThreadBuffer*>& threads();
    /** @brief Storage for the last aggregated frame (main thread only). */
    static Frame& lastFrameStorage();
    static bool& hasFrameFlag();
};

/**
 * @brief RAII scoped zone (used via the EV_PROFILE_* macros).
 */
class EVENGINE_API ZoneScope {
public:
    ZoneScope(const char* name, const char* module = nullptr) {
        Profiler::zoneBegin(name, module);
    }
    ~ZoneScope() { Profiler::zoneEnd(); }
    ZoneScope(const ZoneScope&)            = delete;
    ZoneScope& operator=(const ZoneScope&) = delete;
};

}  // namespace eve::prof

#define EV_PROFILE_CAT_I(a, b) a##b
#define EV_PROFILE_CAT(a, b) EV_PROFILE_CAT_I(a, b)

/** @brief Profile the enclosing scope, auto-named from the current function. */
#define EV_PROFILE_ZONE_FUNC() \
    ::eve::prof::ZoneScope EV_PROFILE_CAT(ev_zone_, __LINE__)(__func__)

/** @brief Profile the enclosing scope with an explicit name. */
#define EV_PROFILE_ZONE(name) \
    ::eve::prof::ZoneScope EV_PROFILE_CAT(ev_zone_, __LINE__)(name)

/** @brief Profile the enclosing scope, tagged with a module for grouping. */
#define EV_PROFILE_MODULE(module, name) \
    ::eve::prof::ZoneScope EV_PROFILE_CAT(ev_zone_, __LINE__)(name, module)

#endif
