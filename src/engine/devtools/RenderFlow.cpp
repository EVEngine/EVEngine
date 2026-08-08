#include "devtools/RenderFlow.hpp"

#include <algorithm>
#include <queue>
#include <sstream>

namespace eve::dev {

RenderFlow::RenderFlow() = default;
RenderFlow::~RenderFlow() = default;

void RenderFlow::clear() {
    events_.clear();
    passStack_.clear();
    nextEventId_ = 1;
    lastEventId_ = 0;
    lastBind_.clear();
    dataDeps_.clear();
}

void RenderFlow::setMaxEvents(size_t n) {
    maxEvents_ = n < 16 ? 16 : n;
    while (events_.size() > maxEvents_) dropOldest();
}

void RenderFlow::dropOldest() {
    if (events_.empty()) return;
    const RenderEvent old = events_.front();
    events_.pop_front();
    dataDeps_.erase(old.id);
    for (auto it = lastBind_.begin(); it != lastBind_.end();) {
        if (it->second == old.id)
            it = lastBind_.erase(it);
        else
            ++it;
    }
    // passStack_ is live nesting; leave it. Dangling ids ignored via event().
}

void RenderFlow::ensureCapacity() {
    while (events_.size() >= maxEvents_) dropOldest();
}

uint32_t RenderFlow::append(RenderEventKind kind, const std::string& name,
                            const std::string& detail) {
    ensureCapacity();
    RenderEvent e;
    e.id       = nextEventId_++;
    e.kind     = kind;
    e.name     = name;
    e.detail   = detail;
    e.parentId = lastEventId_;
    e.passId   = passStack_.empty() ? 0 : passStack_.back();
    events_.push_back(e);
    lastEventId_ = e.id;
    return e.id;
}

const RenderEvent* RenderFlow::event(uint32_t id) const {
    if (id == 0 || events_.empty()) return nullptr;
    const uint32_t first = events_.front().id;
    const uint32_t last  = events_.back().id;
    if (id < first || id > last) return nullptr;
    return &events_[static_cast<size_t>(id - first)];
}

void RenderFlow::frameBegin() { append(RenderEventKind::FrameBegin, "frame", ""); }

void RenderFlow::frameEnd() { append(RenderEventKind::FrameEnd, "frame", ""); }

void RenderFlow::passBegin(const char* name) {
    const uint32_t id = append(RenderEventKind::PassBegin, name ? name : "", "");
    passStack_.push_back(id);
    events_.back().passId = id;
}

void RenderFlow::passEnd(const char* name) {
    append(RenderEventKind::PassEnd, name ? name : "", "");
    if (!passStack_.empty()) passStack_.pop_back();
}

void RenderFlow::target(const char* name) {
    append(RenderEventKind::Target, "target", name ? name : "");
}

void RenderFlow::bind(const char* kind, const char* name) {
    const std::string k = kind ? kind : "resource";
    const std::string n = name ? name : "";
    const uint32_t id = append(RenderEventKind::Bind, k, n);
    lastBind_[k + "|" + n] = id;
    if (!n.empty()) lastBind_["*|" + n] = id;
}

void RenderFlow::draw(const char* api, const char* detail) {
    const uint32_t id = append(RenderEventKind::Draw, api ? api : "draw", detail ? detail : "");
    // Data-flow: draw depends on enclosing pass + any binds whose name appears in detail,
    // and the most recent binds of common kinds (texture/shader/mesh/font).
    if (events_.back().passId) dataDeps_[id].push_back(events_.back().passId);

    auto linkRecent = [&](const char* kind) {
        // Find most recent bind of this kind still in the window / lastBind_
        uint32_t best = 0;
        for (const auto& kv : lastBind_) {
            if (kv.first.rfind(std::string(kind) + "|", 0) == 0) {
                if (kv.second > best && event(kv.second)) best = kv.second;
            }
        }
        if (best) dataDeps_[id].push_back(best);
    };
    linkRecent("texture");
    linkRecent("shader");
    linkRecent("mesh");
    linkRecent("font");
    linkRecent("canvas");

    if (detail && detail[0]) {
        auto it = lastBind_.find(std::string("*|") + detail);
        if (it != lastBind_.end() && event(it->second)) dataDeps_[id].push_back(it->second);
    }
}

void RenderFlow::error(const char* message) {
    const uint32_t id = append(RenderEventKind::Error, "error", message ? message : "");
    if (events_.back().passId) dataDeps_[id].push_back(events_.back().passId);
    // Error depends on the immediately preceding draw/bind in the same pass.
    for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
        if (it->id == id) continue;
        if (it->kind == RenderEventKind::Draw || it->kind == RenderEventKind::Bind ||
            it->kind == RenderEventKind::Target) {
            dataDeps_[id].push_back(it->id);
            break;
        }
        if (it->kind == RenderEventKind::PassBegin) break;
    }
}

uint32_t RenderFlow::findSeed(const RenderSliceCriterion& c) const {
    if (c.eventId && event(c.eventId)) return c.eventId;
    for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
        if (it->kind == RenderEventKind::Error) return it->id;
    }
    return events_.empty() ? 0 : events_.back().id;
}

RenderSliceResult RenderFlow::sliceBackward(const RenderSliceCriterion& c) const {
    RenderSliceResult result;
    const uint32_t seed = findSeed(c);
    if (!seed) {
        result.summary = "empty render slice";
        return result;
    }

    std::unordered_set<uint32_t> visited;
    std::queue<uint32_t>         q;
    auto enqueue = [&](uint32_t id) {
        if (!event(id)) return;
        if (visited.insert(id).second) q.push(id);
    };
    enqueue(seed);

    if (!c.resource.empty()) {
        auto it = lastBind_.find("*|" + c.resource);
        if (it != lastBind_.end()) enqueue(it->second);
    }

    while (!q.empty()) {
        const uint32_t id = q.front();
        q.pop();
        const RenderEvent* e = event(id);
        if (!e) continue;
        enqueue(e->parentId);
        enqueue(e->passId);
        auto dit = dataDeps_.find(id);
        if (dit != dataDeps_.end()) {
            for (uint32_t dep : dit->second) enqueue(dep);
        }
    }

    result.eventIds.assign(visited.begin(), visited.end());
    std::sort(result.eventIds.begin(), result.eventIds.end());
    for (uint32_t id : result.eventIds) {
        if (const RenderEvent* e = event(id)) result.path.push_back(*e);
    }

    // Reconstruct pass nesting at seed from retained window.
    for (const auto& e : events_) {
        if (e.id > seed) break;
        if (e.kind == RenderEventKind::PassBegin) result.passes.push_back(e.name);
        else if (e.kind == RenderEventKind::PassEnd && !result.passes.empty())
            result.passes.pop_back();
    }

    std::ostringstream os;
    os << "render slice: " << result.eventIds.size() << " events, pass depth "
       << result.passes.size();
    result.summary = os.str();
    return result;
}

std::string RenderFlow::formatErrorReport(const std::string& errorMessage,
                                          const RenderSliceCriterion& c) const {
    const RenderSliceResult slice = sliceBackward(c);
    std::ostringstream os;
    os << "=== Render Pipeline Trace ===\n";
    os << "Error: " << errorMessage << "\n";
    os << "\n-- Active passes --\n";
    if (slice.passes.empty()) {
        os << "  (none)\n";
    } else {
        for (size_t i = 0; i < slice.passes.size(); ++i)
            os << "  #" << i << ' ' << slice.passes[i] << "\n";
    }
    os << "\n-- Relevant render events --\n";
    if (slice.path.empty()) {
        os << "  (none)\n";
    } else {
        size_t shown = 0;
        for (const auto& e : slice.path) {
            const char* kind = "?";
            switch (e.kind) {
                case RenderEventKind::FrameBegin: kind = "frame+"; break;
                case RenderEventKind::FrameEnd:   kind = "frame-"; break;
                case RenderEventKind::PassBegin:  kind = "pass+"; break;
                case RenderEventKind::PassEnd:    kind = "pass-"; break;
                case RenderEventKind::Target:     kind = "target"; break;
                case RenderEventKind::Bind:       kind = "bind"; break;
                case RenderEventKind::Draw:       kind = "draw"; break;
                case RenderEventKind::Error:      kind = "error"; break;
            }
            os << "  [" << kind << "] " << e.name;
            if (!e.detail.empty()) os << " (" << e.detail << ")";
            os << "\n";
            if (++shown >= 48) {
                os << "  ...\n";
                break;
            }
        }
    }
    os << "\n" << slice.summary << "\n";
    return os.str();
}

}  // namespace eve::dev
