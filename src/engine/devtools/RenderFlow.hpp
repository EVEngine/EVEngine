#pragma once

#include "common/Export.h"
#include "common/RenderTrace.h"

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eve::dev {

enum class RenderEventKind : uint8_t {
    FrameBegin = 0,
    FrameEnd,
    PassBegin,
    PassEnd,
    Target,
    Bind,
    Draw,
    Error,
};

struct EVENGINE_API RenderEvent {
    uint32_t       id   = 0;
    RenderEventKind kind = RenderEventKind::Draw;
    std::string    name;     // pass / api / resource kind / "error"
    std::string    detail;   // resource name, draw detail, error message
    uint32_t       parentId = 0;  // control predecessor
    uint32_t       passId   = 0;  // enclosing PassBegin event
};

struct EVENGINE_API RenderSliceCriterion {
    uint32_t    eventId = 0;  // 0 ⇒ last Error, else last event
    std::string resource;     // optional: focus on a bound resource name
};

struct EVENGINE_API RenderSliceResult {
    std::vector<uint32_t>    eventIds;
    std::vector<RenderEvent> path;      // chronological events in the slice
    std::vector<std::string> passes;    // nested pass names at criterion
    std::string              summary;
};

/**
 * Render-pipeline flow tracer with a Weiser-style backward slice.
 *
 * Models pass nesting + bind→draw resource data-flow so a Graphics exception
 * can be attributed to the active pass and the resources that fed the draw.
 */
class EVENGINE_API RenderFlow : public eve::debug::IRenderTracer {
public:
    RenderFlow();
    ~RenderFlow() override;

    void clear();
    void setMaxEvents(size_t n);
    size_t maxEvents() const { return maxEvents_; }
    size_t eventCount() const { return events_.size(); }

    const std::deque<RenderEvent>& events() const { return events_; }
    const RenderEvent* event(uint32_t id) const;

    // IRenderTracer
    void frameBegin() override;
    void frameEnd() override;
    void passBegin(const char* name) override;
    void passEnd(const char* name) override;
    void target(const char* name) override;
    void bind(const char* kind, const char* name) override;
    void draw(const char* api, const char* detail) override;
    void error(const char* message) override;

    RenderSliceResult sliceBackward(const RenderSliceCriterion& c = {}) const;
    std::string formatErrorReport(const std::string& errorMessage,
                                  const RenderSliceCriterion& c = {}) const;

private:
    uint32_t append(RenderEventKind kind, const std::string& name, const std::string& detail);
    void     dropOldest();
    void     ensureCapacity();
    uint32_t findSeed(const RenderSliceCriterion& c) const;

    size_t                 maxEvents_   = 50000;
    uint32_t               nextEventId_ = 1;
    uint32_t               lastEventId_ = 0;
    std::deque<RenderEvent> events_;
    std::vector<uint32_t>  passStack_;  // PassBegin event ids

    // last Bind event id for (kind, name) — resource reaching-def for draws
    std::unordered_map<std::string, uint32_t> lastBind_;
    // Draw/Error → Bind / Pass deps
    std::unordered_map<uint32_t, std::vector<uint32_t>> dataDeps_;
};

}  // namespace eve::dev
