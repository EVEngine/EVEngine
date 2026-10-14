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
    /** @brief Pass begin. */
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

/** @brief RAII pass scope for C++ call sites. */
class RenderPassScope {
public:
    /** @brief Constructs a RenderPassScope. */
    explicit RenderPassScope(const char* name) : name_(name ? name : "") {
        /** @brief Rt pass begin. */
        rtPassBegin(name_.c_str());
    }
    /** @brief Releases RenderPassScope resources. */
    ~RenderPassScope() { rtPassEnd(name_.c_str()); }
    RenderPassScope(const RenderPassScope&)            = delete;
    RenderPassScope& operator=(const RenderPassScope&) = delete;

private:
    std::string name_;
};

}  // namespace eve::debug
