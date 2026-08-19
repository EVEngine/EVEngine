#pragma once

#include "common/Export.h"
#include "common/RenderTrace.h"

#include <cstdint>
#include <iterator>
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
 * @brief Render-pipeline flow tracer with a Weiser-style backward slice.
 *
 * Models pass nesting + bind→draw resource data-flow so a Graphics exception
 * can be attributed to the active pass and the resources that fed the draw.
 *
 * Event storage is a fixed ring buffer: when full, new events overwrite the
 * oldest slot in place (O(1)).
 */
class EVENGINE_API RenderFlow : public eve::debug::IRenderTracer {
public:
    RenderFlow();
    ~RenderFlow() override;

    void clear();
    void setMaxEvents(size_t n);
    size_t maxEvents() const { return maxEvents_; }
    size_t eventCount() const { return count_; }

    /** @brief Chronological view over the live ring window (oldest → newest). */
    class EVENGINE_API EventsView {
    public:
        class EVENGINE_API const_iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using value_type        = RenderEvent;
            using difference_type   = std::ptrdiff_t;
            using pointer           = const RenderEvent*;
            using reference         = const RenderEvent&;

            const_iterator() = default;
            reference operator*() const { return (*g_)[idx_]; }
            pointer   operator->() const { return &(*g_)[idx_]; }
            const_iterator& operator++() {
                ++idx_;
                return *this;
            }
            const_iterator operator++(int) {
                const_iterator t = *this;
                ++(*this);
                return t;
            }
            bool operator==(const const_iterator& o) const {
                return g_ == o.g_ && idx_ == o.idx_;
            }
            bool operator!=(const const_iterator& o) const { return !(*this == o); }

        private:
            friend class EventsView;
            const_iterator(const RenderFlow* g, size_t idx) : g_(g), idx_(idx) {}
            const RenderFlow* g_   = nullptr;
            size_t            idx_ = 0;
        };

        explicit EventsView(const RenderFlow* g) : g_(g) {}

        const_iterator begin() const { return const_iterator(g_, 0); }
        const_iterator end() const { return const_iterator(g_, g_ ? g_->count_ : 0); }
        bool   empty() const { return !g_ || g_->count_ == 0; }
        size_t size() const { return g_ ? g_->count_ : 0; }
        const RenderEvent& operator[](size_t i) const { return (*g_)[i]; }
        const RenderEvent& front() const { return (*g_)[0]; }
        const RenderEvent& back() const { return (*g_)[g_->count_ - 1]; }

    private:
        const RenderFlow* g_ = nullptr;
    };

    EventsView events() const { return EventsView(this); }
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
    friend class EventsView;
    friend class EventsView::const_iterator;

    uint32_t append(RenderEventKind kind, const std::string& name, const std::string& detail);
    void     retireSlot(size_t physical);
    void     ensureRing();
    uint32_t findSeed(const RenderSliceCriterion& c) const;

    size_t physicalIndex(size_t chrono) const {
        return (head_ + chrono) % maxEvents_;
    }
    const RenderEvent& operator[](size_t chrono) const {
        return slots_[physicalIndex(chrono)];
    }
    RenderEvent& operator[](size_t chrono) { return slots_[physicalIndex(chrono)]; }
    RenderEvent& newest() { return (*this)[count_ - 1]; }

    size_t maxEvents_   = 50000;
    uint32_t nextEventId_ = 1;
    uint32_t lastEventId_ = 0;

    std::vector<RenderEvent> slots_;
    size_t                   head_  = 0;
    size_t                   count_ = 0;

    std::vector<uint32_t> passStack_;  // PassBegin event ids

    // last Bind event id for (kind, name) — resource reaching-def for draws
    std::unordered_map<std::string, uint32_t> lastBind_;
    // Draw/Error → Bind / Pass deps
    std::unordered_map<uint32_t, std::vector<uint32_t>> dataDeps_;
};

}  // namespace eve::dev
