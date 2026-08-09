#include "devtools/RenderFlow.hpp"

#include <algorithm>
#include <queue>
#include <sstream>

namespace eve::dev {

RenderFlow::RenderFlow() = default;

RenderFlow::~RenderFlow() = default;

void RenderFlow::clear() {
    head_  = 0;
    count_ = 0;
    passStack_.clear();
    nextEventId_ = 1;
    lastEventId_ = 0;
    lastBind_.clear();
    dataDeps_.clear();
}

void RenderFlow::ensureRing() {
    if (slots_.size() != maxEvents_) slots_.resize(maxEvents_);
}

void RenderFlow::setMaxEvents(size_t n) {
    n = n < 16 ? 16 : n;
    if (n == maxEvents_ && slots_.size() == n) return;

    const size_t keep = count_ < n ? count_ : n;
    const size_t drop = count_ - keep;
    std::vector<RenderEvent> neu(n);
    for (size_t i = 0; i < drop; ++i) retireSlot(physicalIndex(i));
    for (size_t i = 0; i < keep; ++i) neu[i] = (*this)[drop + i];

    slots_     = std::move(neu);
    head_      = 0;
    count_     = keep;
    maxEvents_ = n;
}

void RenderFlow::retireSlot(size_t physical) {
    RenderEvent& old = slots_[physical];
    if (old.id == 0) return;

    dataDeps_.erase(old.id);

    if (old.kind == RenderEventKind::Bind) {
        const std::string k = old.name + "|" + old.detail;
        auto it = lastBind_.find(k);
        if (it != lastBind_.end() && it->second == old.id) lastBind_.erase(it);
        if (!old.detail.empty()) {
            const std::string star = std::string("*|") + old.detail;
            auto sit = lastBind_.find(star);
            if (sit != lastBind_.end() && sit->second == old.id) lastBind_.erase(sit);
        }
    }

    old = RenderEvent{};
}

uint32_t RenderFlow::append(RenderEventKind kind, const std::string& name,
                            const std::string& detail) {
    ensureRing();

    size_t phys;
    if (count_ < maxEvents_) {
        phys = physicalIndex(count_);
        ++count_;
    } else {
        phys = head_;
        retireSlot(phys);
        head_ = (head_ + 1) % maxEvents_;
    }

    RenderEvent& e = slots_[phys];
    e.id           = nextEventId_++;
    e.kind         = kind;
    e.name         = name;
    e.detail       = detail;
    e.parentId     = lastEventId_;
    e.passId       = passStack_.empty() ? 0 : passStack_.back();
    lastEventId_   = e.id;
    return e.id;
}

const RenderEvent* RenderFlow::event(uint32_t id) const {
    if (id == 0 || count_ == 0) return nullptr;
    const uint32_t first = (*this)[0].id;
    const uint32_t last  = (*this)[count_ - 1].id;
    if (id < first || id > last) return nullptr;
    return &(*this)[static_cast<size_t>(id - first)];
}

void RenderFlow::frameBegin() { append(RenderEventKind::FrameBegin, "frame", ""); }

void RenderFlow::frameEnd() { append(RenderEventKind::FrameEnd, "frame", ""); }

void RenderFlow::passBegin(const char* name) {
    const uint32_t id = append(RenderEventKind::PassBegin, name ? name : "", "");
    passStack_.push_back(id);
    newest().passId = id;
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
    if (newest().passId) dataDeps_[id].push_back(newest().passId);

    auto linkRecent = [&](const char* kind) {
        uint32_t best = 0;
        const std::string prefix = std::string(kind) + "|";
        for (const auto& kv : lastBind_) {
            if (kv.first.rfind(prefix, 0) == 0) {
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
    if (newest().passId) dataDeps_[id].push_back(newest().passId);
    // Error depends on the immediately preceding draw/bind in the same pass.
    for (size_t i = count_; i-- > 0;) {
        const RenderEvent& e = (*this)[i];
        if (e.id == id) continue;
        if (e.kind == RenderEventKind::Draw || e.kind == RenderEventKind::Bind ||
            e.kind == RenderEventKind::Target) {
            dataDeps_[id].push_back(e.id);
            break;
        }
        if (e.kind == RenderEventKind::PassBegin) break;
    }
}

uint32_t RenderFlow::findSeed(const RenderSliceCriterion& c) const {
    if (c.eventId && event(c.eventId)) return c.eventId;
    for (size_t i = count_; i-- > 0;) {
        if ((*this)[i].kind == RenderEventKind::Error) return (*this)[i].id;
    }
    return count_ == 0 ? 0 : (*this)[count_ - 1].id;
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
    for (size_t i = 0; i < count_; ++i) {
        const RenderEvent& e = (*this)[i];
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
