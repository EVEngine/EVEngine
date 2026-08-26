#pragma once

#include "common/Module.h"
#include "common/Profile.h"
#include "common/RenderTrace.h"

#include <squirrel.h>

#include <cstdint>
#include <string>

namespace ssq {
class Object;
}

namespace eve::profiler {

/**
 * @brief Script-facing wrapper around the engine-wide profiler core
 *        (eve::prof, see common/Profile.h).
 *
 * The core collects scoped zones from every engine system (physics, animation,
 * particles, audio, ui, scene, graphics passes, ...) into a per-frame call tree
 * with self/total time per module and per zone. This module exposes that data
 * to Squirrel and adds GPU frame timing (via eve::service::IGpuTimer).
 *
 * Script usage:
 * @code
 * profiler.setEnabled(true);         // start collecting engine-wide zones
 * // ... run a few frames ...
 * print(profiler.textReport());      // per-module/per-zone hotspot report
 * local rows = profiler.capture();   // array of {module,name,selfMs,totalMs,count,thread}
 * profiler.setEnabled(false);
 * @endcode
 *
 * The module installs itself as the IRenderTracer, so each render pass becomes a
 * "graphics" zone and the render frame boundary drives automatic per-frame
 * aggregation (no game script changes required). beginFrame()/endFrame() are also
 * exposed for manual frame delimitation around arbitrary code.
 */
class Profiler : public Module, public eve::debug::IRenderTracer {
public:
    Module_REG(Profiler);
    Profiler();
    ~Profiler() override;

    /** @brief Enables/disables engine-wide collection. Disabled = cheap no-op. */
    void setEnabled(bool on);
    /** @brief True when collection is active. */
    bool enabled() const;
    /** @brief Clears all collected data. */
    void reset();

    /** @brief Marks the start of a frame for total-frame timing. */
    void beginFrame();
    /** @brief Finalizes the current frame and aggregates the collected zones. */
    void endFrame();

    /**
     * @brief Opens a user timing scope under the current frame.
     * @param name Scope name (must be non-null).
     */
    void begin(const char* name);
    /** @brief Closes the innermost open user scope. */
    void end();

    /** @brief True once at least one frame has been aggregated. */
    bool hasFrame() const;
    /** @brief Total CPU frame time of the last frame, in milliseconds. */
    float frameMs() const;
    /** @brief GPU execution time of the last presented frame (0 when unavailable). */
    float gpuFrameMs() const;
    /** @brief Human-readable hotspot report (per module / per zone). */
    std::string textReport() const;
    /**
     * @brief Structured report of the last frame.
     * @return Array of tables {module, name, thread, selfMs, totalMs, count},
     *         sorted by self time descending.
     */
    ssq::Object capture();

    // IRenderTracer: route render passes into the core as "graphics" zones and
    // drive automatic per-frame aggregation from the render frame boundary.
    void frameBegin() override;
    void frameEnd() override;
    void passBegin(const char* name) override;
    void passEnd(const char* name) override;
    void draw(const char* api, const char* detail) override {}

private:
    int64_t frameBeginNs_ = 0;
    float   frameMs_      = 0.f;
    HSQUIRRELVM vm_       = nullptr;
};

}  // namespace eve::profiler
