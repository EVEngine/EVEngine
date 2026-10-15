#pragma once

#include "common/Export.h"

#include <string>

namespace eve::debug {

/**
 * @brief Optional render-pipeline tracer. Graphics / RenderSystem call the rt* helpers;
 * when no tracer is installed they are cheap null checks (Android/iOS keep this
 * unset because EVDevTools is not linked there).
 */
class EVENGINE_API_FOUNDATION_INLINE IRenderTracer {
public:
    /** @brief I render tracer. */
    virtual ~IRenderTracer() = default;
    /** @brief Frame begin. */
    virtual void frameBegin() {}
    /** @brief Frame end. */
    virtual void frameEnd() {}
    /**
     * @brief Opens a traced pass.
     * @param name Pass name, which must stay readable until the frame is marked.
     * @note Ownership/lifetime: the pointer may be *retained* -- the profiler stores it in
     *       `eve::prof::ZoneRecord::name` and reads it in `frameMark()`, a whole frame
     *       later. Callers must pass static storage (a string literal), never a pointer
     *       into a temporary or a scope-local buffer.
     */
    virtual void passBegin(const char* name) {}
    /** @brief Pass end. */
    virtual void passEnd(const char* name) {}
    /** @brief Target. */
    virtual void target(const char* name) {}  // screen / canvas
    /** @brief Binds . */
    virtual void bind(const char* kind, const char* name) {}
    /** @brief Draws . */
    virtual void draw(const char* api, const char* detail) {}
    /** @brief Error. */
    virtual void error(const char* message) {}
};

/** @brief Sets the render tracer. */
EVENGINE_API_FOUNDATION void           setRenderTracer(IRenderTracer* tracer);
/** @brief Renders tracer. */
EVENGINE_API_FOUNDATION IRenderTracer* renderTracer();

/** @brief Rt frame begin. */
inline void rtFrameBegin() {
    if (IRenderTracer* t = renderTracer()) t->frameBegin();
}
/** @brief Rt frame end. */
inline void rtFrameEnd() {
    if (IRenderTracer* t = renderTracer()) t->frameEnd();
}
/** @brief Rt pass begin. */
inline void rtPassBegin(const char* name) {
    if (IRenderTracer* t = renderTracer()) t->passBegin(name);
}
/** @brief Rt pass end. */
inline void rtPassEnd(const char* name) {
    if (IRenderTracer* t = renderTracer()) t->passEnd(name);
}
/** @brief Rt target. */
inline void rtTarget(const char* name) {
    if (IRenderTracer* t = renderTracer()) t->target(name);
}
/** @brief Rt bind. */
inline void rtBind(const char* kind, const char* name) {
    if (IRenderTracer* t = renderTracer()) t->bind(kind, name);
}
/** @brief Rt draw. */
inline void rtDraw(const char* api, const char* detail = nullptr) {
    if (IRenderTracer* t = renderTracer()) t->draw(api, detail ? detail : "");
}
/** @brief Rt error. */
inline void rtError(const char* message) {
    if (IRenderTracer* t = renderTracer()) t->error(message ? message : "");
}

/**
 * @brief RAII pass scope for C++ call sites.
 *
 * The name is *forwarded*, not copied. `passBegin` may retain the pointer (the profiler
 * keeps it in `ZoneRecord::name` until `frameMark()`), so a scope that handed out its own
 * `std::string` buffer left the profiler reading freed memory for the built-in passes --
 * non-deterministic, which is why it went unnoticed. Callers therefore pass static
 * storage: every call site in the engine passes a string literal.
 */
class RenderPassScope {
public:
    explicit RenderPassScope(const char* name) : name_(name ? name : "") { rtPassBegin(name_); }
    ~RenderPassScope() { rtPassEnd(name_); }
    RenderPassScope(const RenderPassScope&)            = delete;
    RenderPassScope& operator=(const RenderPassScope&) = delete;

private:
    const char* name_;
};

}  // namespace eve::debug
