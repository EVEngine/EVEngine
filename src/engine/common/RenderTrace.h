#pragma once

#include "common/Export.h"

#include <string>

namespace eve::debug {

/**
 * Optional render-pipeline tracer. Graphics / RenderSystem call the rt* helpers;
 * when no tracer is installed they are cheap null checks (Android/iOS keep this
 * unset because EVDevTools is not linked there).
 */
class EVENGINE_API IRenderTracer {
public:
    virtual ~IRenderTracer() = default;
    virtual void frameBegin() {}
    virtual void frameEnd() {}
    virtual void passBegin(const char* name) {}
    virtual void passEnd(const char* name) {}
    virtual void target(const char* name) {}  // screen / canvas
    virtual void bind(const char* kind, const char* name) {}
    virtual void draw(const char* api, const char* detail) {}
    virtual void error(const char* message) {}
};

EVENGINE_API void setRenderTracer(IRenderTracer* tracer);
EVENGINE_API IRenderTracer* renderTracer();

inline void rtFrameBegin() {
    if (IRenderTracer* t = renderTracer()) t->frameBegin();
}
inline void rtFrameEnd() {
    if (IRenderTracer* t = renderTracer()) t->frameEnd();
}
inline void rtPassBegin(const char* name) {
    if (IRenderTracer* t = renderTracer()) t->passBegin(name);
}
inline void rtPassEnd(const char* name) {
    if (IRenderTracer* t = renderTracer()) t->passEnd(name);
}
inline void rtTarget(const char* name) {
    if (IRenderTracer* t = renderTracer()) t->target(name);
}
inline void rtBind(const char* kind, const char* name) {
    if (IRenderTracer* t = renderTracer()) t->bind(kind, name);
}
inline void rtDraw(const char* api, const char* detail = nullptr) {
    if (IRenderTracer* t = renderTracer()) t->draw(api, detail ? detail : "");
}
inline void rtError(const char* message) {
    if (IRenderTracer* t = renderTracer()) t->error(message ? message : "");
}

/** RAII pass scope for C++ call sites. */
class RenderPassScope {
public:
    explicit RenderPassScope(const char* name) : name_(name ? name : "") {
        rtPassBegin(name_.c_str());
    }
    ~RenderPassScope() { rtPassEnd(name_.c_str()); }
    RenderPassScope(const RenderPassScope&)            = delete;
    RenderPassScope& operator=(const RenderPassScope&) = delete;

private:
    std::string name_;
};

}  // namespace eve::debug
