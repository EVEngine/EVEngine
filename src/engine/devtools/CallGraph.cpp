#include "devtools/CallGraph.hpp"

#include <algorithm>
#include <queue>
#include <sstream>

namespace eve::dev {

bool SourceLoc::matches(const SourceLoc& o) const {
    if (line > 0 && o.line > 0 && line != o.line) return false;
    if (!source.empty() && !o.source.empty() && source != o.source) return false;
    if (!function.empty() && !o.function.empty() && function != o.function) return false;
    return true;
}

std::string SourceLoc::toString() const {
    std::ostringstream os;
    if (!source.empty())
        os << source;
    else
        os << "<unknown>";
    if (line > 0) os << ':' << line;
    if (!function.empty()) os << " in " << function;
    return os.str();
}

CallGraph::CallGraph() = default;
CallGraph::~CallGraph() = default;

void CallGraph::clear() {
    events_.clear();
    frameStack_.clear();
    nextFrameId_ = 1;
    nextEventId_ = 1;
    lastEventId_ = 0;
    lastDef_.clear();
    dataDeps_.clear();
    frameToCallEvent_.clear();
}

void CallGraph::setMaxEvents(size_t n) {
    maxEvents_ = n < 16 ? 16 : n;
    while (events_.size() > maxEvents_) dropOldest();
}

void CallGraph::dropOldest() {
    if (events_.empty()) return;
    const TraceEvent old = events_.front();
    events_.pop_front();

    dataDeps_.erase(old.id);

    for (auto fit = lastDef_.begin(); fit != lastDef_.end();) {
        for (auto vit = fit->second.begin(); vit != fit->second.end();) {
            if (vit->second == old.id)
                vit = fit->second.erase(vit);
            else
                ++vit;
        }
        if (fit->second.empty())
            fit = lastDef_.erase(fit);
        else
            ++fit;
    }

    for (auto it = frameToCallEvent_.begin(); it != frameToCallEvent_.end();) {
        if (it->second == old.id)
            it = frameToCallEvent_.erase(it);
        else
            ++it;
    }
    // Live frameStack_ is not rebuilt: it reflects the current activation, not
    // the retained window. Dangling parentEventId / data-dep ids are ignored
    // via event() == nullptr during queries.
}

void CallGraph::ensureCapacity() {
    while (events_.size() >= maxEvents_) dropOldest();
}

uint32_t CallGraph::append(TraceKind kind, const SourceLoc& loc, const std::string& name) {
    ensureCapacity();
    TraceEvent e;
    e.id            = nextEventId_++;
    e.kind          = kind;
    e.loc           = loc;
    e.name          = name;
    e.frameId       = frameStack_.empty() ? 0 : frameStack_.back();
    e.parentEventId = lastEventId_;
    if (e.loc.function.empty() && !name.empty() &&
        (kind == TraceKind::Call || kind == TraceKind::Return)) {
        e.loc.function = name;
    }
    events_.push_back(e);
    lastEventId_ = e.id;
    return e.id;
}

uint32_t CallGraph::onCall(const SourceLoc& loc, const std::string& funcName) {
    const std::string fn = !funcName.empty() ? funcName : loc.function;
    const uint32_t id    = append(TraceKind::Call, loc, fn);
    const uint32_t frame = nextFrameId_++;
    events_.back().frameId = frame;
    frameStack_.push_back(frame);
    frameToCallEvent_[frame] = id;
    return id;
}

uint32_t CallGraph::onReturn(const SourceLoc& loc, const std::string& funcName) {
    const std::string fn = !funcName.empty() ? funcName : loc.function;
    const uint32_t id    = append(TraceKind::Return, loc, fn);
    if (!frameStack_.empty()) {
        events_.back().frameId = frameStack_.back();
        frameStack_.pop_back();
    }
    return id;
}

uint32_t CallGraph::onLine(const SourceLoc& loc) {
    return append(TraceKind::Line, loc, {});
}

uint32_t CallGraph::onDef(const SourceLoc& loc, const std::string& var) {
    if (var.empty()) return 0;
    const uint32_t id      = append(TraceKind::Def, loc, var);
    const uint32_t frameId = events_.back().frameId;
    lastDef_[frameId][var] = id;
    // Free / outer locals: if an enclosing activation already tracks this name,
    // update its reaching definition (Squirrel closures mutate outer bindings).
    for (uint32_t fid : frameStack_) {
        if (fid == frameId) continue;
        auto fit = lastDef_.find(fid);
        if (fit != lastDef_.end() && fit->second.count(var)) fit->second[var] = id;
    }
    return id;
}

uint32_t CallGraph::onUse(const SourceLoc& loc, const std::string& var) {
    if (var.empty()) return 0;
    const uint32_t id = append(TraceKind::Use, loc, var);
    linkData(id, var);
    return id;
}

void CallGraph::linkData(uint32_t useEventId, const std::string& var) {
    const TraceEvent* use = event(useEventId);
    if (!use || var.empty()) return;

    // 1) Same-frame reaching definition
    auto fit = lastDef_.find(use->frameId);
    if (fit != lastDef_.end()) {
        auto vit = fit->second.find(var);
        if (vit != fit->second.end() && vit->second < useEventId && event(vit->second)) {
            dataDeps_[useEventId].push_back(vit->second);
            return;
        }
    }

    // 2) Walk caller frames for outer-scope / captured defs (best-effort)
    for (auto it = frameStack_.rbegin(); it != frameStack_.rend(); ++it) {
        if (*it == use->frameId) continue;
        auto fit2 = lastDef_.find(*it);
        if (fit2 == lastDef_.end()) continue;
        auto vit2 = fit2->second.find(var);
        if (vit2 != fit2->second.end() && vit2->second < useEventId && event(vit2->second)) {
            dataDeps_[useEventId].push_back(vit2->second);
            return;
        }
    }

    // 3) Historical scan inside the retained window
    for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
        if (it->id >= useEventId) continue;
        if (it->kind == TraceKind::Def && it->name == var) {
            dataDeps_[useEventId].push_back(it->id);
            return;
        }
    }
}

uint32_t CallGraph::enter(const SourceLoc& loc, const std::string& funcName) {
    onCall(loc, funcName);
    return onLine(loc);
}

const TraceEvent* CallGraph::event(uint32_t id) const {
    if (id == 0 || events_.empty()) return nullptr;
    const uint32_t first = events_.front().id;
    const uint32_t last  = events_.back().id;
    if (id < first || id > last) return nullptr;
    // Ids in the window are contiguous.
    return &events_[static_cast<size_t>(id - first)];
}

std::vector<CallFrame> CallGraph::currentStack() const {
    std::vector<CallFrame> out;
    out.reserve(frameStack_.size());
    for (uint32_t fid : frameStack_) {
        CallFrame f;
        f.frameId = fid;
        auto it = frameToCallEvent_.find(fid);
        if (it != frameToCallEvent_.end()) {
            f.callEventId = it->second;
            if (const TraceEvent* e = event(it->second)) f.loc = e->loc;
        }
        out.push_back(f);
    }
    return out;
}

std::vector<CallFrame> CallGraph::stackAt(uint32_t eventId) const {
    std::vector<CallFrame> stack;
    if (!event(eventId)) return stack;
    for (const auto& e : events_) {
        if (e.id > eventId) break;
        if (e.kind == TraceKind::Call) {
            CallFrame f;
            f.frameId     = e.frameId;
            f.loc         = e.loc;
            f.callEventId = e.id;
            stack.push_back(f);
        } else if (e.kind == TraceKind::Return) {
            if (!stack.empty()) stack.pop_back();
        }
    }
    return stack;
}

std::vector<std::pair<SourceLoc, SourceLoc>> CallGraph::callEdges() const {
    std::vector<std::pair<SourceLoc, SourceLoc>> edges;
    std::vector<SourceLoc> stack;
    for (const auto& e : events_) {
        if (e.kind == TraceKind::Call) {
            if (!stack.empty()) edges.emplace_back(stack.back(), e.loc);
            stack.push_back(e.loc);
        } else if (e.kind == TraceKind::Return) {
            if (!stack.empty()) stack.pop_back();
        }
    }
    return edges;
}

uint32_t CallGraph::findSeedEvent(const SliceCriterion& c) const {
    if (c.eventId != 0 && event(c.eventId)) return c.eventId;
    if (c.loc.empty()) return events_.empty() ? 0 : events_.back().id;

    for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
        if (c.loc.matches(it->loc)) return it->id;
    }
    return events_.empty() ? 0 : events_.back().id;
}

void CallGraph::collectSeeds(const SliceCriterion& c, std::vector<uint32_t>& out) const {
    const uint32_t seed = findSeedEvent(c);
    if (seed == 0) return;
    out.push_back(seed);

    const TraceEvent* se = event(seed);
    if (!se) return;

    for (const auto& e : events_) {
        if (e.id > seed) break;
        if (!c.loc.empty() && !c.loc.matches(e.loc)) continue;
        if (e.kind == TraceKind::Def || e.kind == TraceKind::Use || e.kind == TraceKind::Line) {
            if (e.id != seed) out.push_back(e.id);
        }
    }

    if (!c.variables.empty()) {
        for (const auto& var : c.variables) {
            for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
                if (it->id >= seed) continue;
                if (it->kind == TraceKind::Def && it->name == var) {
                    out.push_back(it->id);
                    break;
                }
                if (it->kind == TraceKind::Use && it->name == var) {
                    out.push_back(it->id);
                    break;
                }
            }
        }
    } else {
        for (const auto& e : events_) {
            if (e.id > seed) break;
            if (e.frameId == se->frameId && e.kind == TraceKind::Use &&
                (c.loc.empty() || c.loc.matches(e.loc))) {
                out.push_back(e.id);
            }
        }
    }
}

SliceResult CallGraph::sliceBackward(const SliceCriterion& criterion) const {
    SliceResult result;
    std::vector<uint32_t> seeds;
    collectSeeds(criterion, seeds);
    if (seeds.empty()) {
        result.summary = "empty slice (no matching events)";
        return result;
    }

    const uint32_t primary = findSeedEvent(criterion);
    result.callStack       = stackAt(primary);

    std::unordered_set<uint32_t> visited;
    std::queue<uint32_t>         q;
    for (uint32_t s : seeds) {
        if (visited.insert(s).second) q.push(s);
    }

    auto enqueue = [&](uint32_t id) {
        if (!event(id)) return;
        if (visited.insert(id).second) q.push(id);
    };

    while (!q.empty()) {
        const uint32_t id = q.front();
        q.pop();
        const TraceEvent* e = event(id);
        if (!e) continue;

        auto dit = dataDeps_.find(id);
        if (dit != dataDeps_.end()) {
            for (uint32_t dep : dit->second) {
                if (!event(dep)) continue;
                DataFlowEdge edge;
                edge.fromEventId = dep;
                edge.toEventId   = id;
                edge.var         = e->name;
                result.dataFlow.push_back(edge);
                enqueue(dep);
            }
        }

        enqueue(e->parentEventId);

        if (e->frameId != 0) {
            auto cit = frameToCallEvent_.find(e->frameId);
            if (cit != frameToCallEvent_.end()) {
                enqueue(cit->second);
            } else {
                for (const auto& ev : events_) {
                    if (ev.kind == TraceKind::Call && ev.frameId == e->frameId) {
                        enqueue(ev.id);
                        break;
                    }
                }
            }
        }

        if (e->kind == TraceKind::Return && e->parentEventId != 0) enqueue(e->parentEventId);
    }

    result.eventIds.assign(visited.begin(), visited.end());
    std::sort(result.eventIds.begin(), result.eventIds.end());

    std::unordered_set<std::string> seenLoc;
    for (uint32_t id : result.eventIds) {
        const TraceEvent* e = event(id);
        if (!e || e->loc.empty()) continue;
        const std::string key = e->loc.toString();
        if (seenLoc.insert(key).second) result.locations.push_back(e->loc);
    }

    std::ostringstream os;
    os << "backward slice: " << result.eventIds.size() << " events, "
       << result.locations.size() << " locations, " << result.dataFlow.size()
       << " data-flow edges, stack depth " << result.callStack.size();
    result.summary = os.str();
    return result;
}

std::string CallGraph::formatErrorReport(const std::string& errorMessage,
                                         const SliceCriterion& criterion) const {
    const SliceResult slice = sliceBackward(criterion);
    std::ostringstream os;
    os << "=== Script Error Trace (dynamic slice) ===\n";
    os << "Error: " << errorMessage << "\n";
    if (!criterion.loc.empty()) os << "Site:  " << criterion.loc.toString() << "\n";
    if (!criterion.variables.empty()) {
        os << "Vars:  ";
        for (size_t i = 0; i < criterion.variables.size(); ++i) {
            if (i) os << ", ";
            os << criterion.variables[i];
        }
        os << "\n";
    }
    os << "\n-- Call stack --\n";
    if (slice.callStack.empty()) {
        os << "  (empty)\n";
    } else {
        for (size_t i = 0; i < slice.callStack.size(); ++i) {
            const auto& f = slice.callStack[slice.callStack.size() - 1 - i];
            os << "  #" << i << ' ' << f.loc.toString() << "\n";
        }
    }
    os << "\n-- Data flow (def → use) --\n";
    if (slice.dataFlow.empty()) {
        os << "  (none recorded; feed onDef/onUse or enable local sampling)\n";
    } else {
        std::vector<DataFlowEdge> edges = slice.dataFlow;
        std::sort(edges.begin(), edges.end(),
                  [](const DataFlowEdge& a, const DataFlowEdge& b) {
                      return a.toEventId > b.toEventId;
                  });
        std::unordered_set<std::string> seen;
        size_t shown = 0;
        for (const auto& edge : edges) {
            const TraceEvent* from = event(edge.fromEventId);
            const TraceEvent* to   = event(edge.toEventId);
            if (!from || !to) continue;
            std::ostringstream line;
            line << edge.var << ": " << from->loc.toString() << " → " << to->loc.toString();
            const std::string s = line.str();
            if (!seen.insert(s).second) continue;
            os << "  " << s << "\n";
            if (++shown >= 32) {
                os << "  ...\n";
                break;
            }
        }
    }
    os << "\n-- Relevant code (slice) --\n";
    if (slice.locations.empty()) {
        os << "  (no locations)\n";
    } else {
        for (const auto& loc : slice.locations) os << "  " << loc.toString() << "\n";
    }
    os << "\n" << slice.summary << "\n";
    return os.str();
}

}  // namespace eve::dev
