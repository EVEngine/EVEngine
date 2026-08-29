#include "common/Profile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace eve::prof {

namespace {
std::atomic<bool> g_enabled{false};

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// Module id interning (thread-safe).
std::mutex                   g_modulesMutex;
std::vector<std::string>     g_moduleNames{""};  // index 0 = "default"
std::unordered_map<std::string, uint32_t> g_moduleIds;

uint64_t zoneKey(uint32_t module, const char* name) {
    return (static_cast<uint64_t>(module) << 32) ^ std::hash<const char*>{}(name);
}
}  // namespace

struct Profiler::ThreadBuffer {
    uint32_t threadId = 0;
    std::string threadName = "main";
    std::vector<ZoneRecord> records;
    std::vector<int32_t>    stack;  // indices of open zones
};

struct Profiler::Frame {
    int64_t beginNs = 0;
    int64_t endNs   = 0;
    std::vector<ZoneSample> samples;
};

namespace {
std::mutex                      g_threadsMutex;
}  // namespace

Profiler::ThreadBuffer& Profiler::currentBuffer() {
    static thread_local ThreadBuffer tb;
    if (tb.threadId == 0) {
        tb.threadId =
            static_cast<uint32_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
        const bool isMain = std::this_thread::get_id() == std::thread::id();
        tb.threadName = isMain ? "main" : "worker";
        std::lock_guard<std::mutex> lock(g_threadsMutex);
        threads().push_back(&tb);
    }
    return tb;
}

std::vector<Profiler::ThreadBuffer*>& Profiler::threads() {
    static std::vector<ThreadBuffer*> t;
    return t;
}

Profiler::Frame& Profiler::lastFrameStorage() {
    static Frame f;
    return f;
}

bool& Profiler::hasFrameFlag() {
    static bool b = false;
    return b;
}

uint32_t Profiler::moduleId(const char* module) {
    if (!module || !module[0]) return 0;
    std::lock_guard<std::mutex> lock(g_modulesMutex);
    auto it = g_moduleIds.find(module);
    if (it != g_moduleIds.end()) return it->second;
    const uint32_t id = static_cast<uint32_t>(g_moduleNames.size());
    g_moduleNames.push_back(module);
    g_moduleIds.emplace(module, id);
    return id;
}

// --- public API -------------------------------------------------------------

void Profiler::setEnabled(bool on) { g_enabled.store(on, std::memory_order_relaxed); }

bool Profiler::enabled() { return g_enabled.load(std::memory_order_relaxed); }

void Profiler::zoneBegin(const char* name, const char* module) {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    ThreadBuffer& tb = currentBuffer();
    ZoneRecord r;
    r.name     = name ? name : "";
    r.module   = moduleId(module);
    r.threadId = tb.threadId;
    r.parent   = tb.stack.empty() ? -1 : tb.stack.back();
    r.depth    = static_cast<int16_t>(tb.stack.size());
    r.startNs  = nowNs();
    r.endNs    = 0;
    r.kind     = 0;
    const int32_t idx = static_cast<int32_t>(tb.records.size());
    tb.records.push_back(r);
    tb.stack.push_back(idx);
}

void Profiler::zoneEnd() {
    if (!g_enabled.load(std::memory_order_relaxed)) return;
    ThreadBuffer& tb = currentBuffer();
    if (tb.stack.empty()) return;
    const int32_t idx = tb.stack.back();
    tb.stack.pop_back();
    tb.records[static_cast<size_t>(idx)].endNs = nowNs();
}

void Profiler::reset() {
    std::lock_guard<std::mutex> lock(g_threadsMutex);
    for (auto* tb : threads()) {
        tb->records.clear();
        tb->stack.clear();
    }
    lastFrameStorage() = Frame{};
    hasFrameFlag()  = false;
}

double Profiler::frameMark() {
    if (!g_enabled.load(std::memory_order_relaxed)) return 0.0;

    const int64_t t0 = nowNs();

    // Snapshot and clear every thread buffer.
    struct Snap {
        uint32_t tid;
        std::string tname;
        std::vector<ZoneRecord> records;
    };
    std::vector<Snap> snaps;
    {
        std::lock_guard<std::mutex> lock(g_threadsMutex);
        for (auto* tb : threads()) {
            Snap s;
            s.tid    = tb->threadId;
            s.tname  = tb->threadName;
            for (auto& r : tb->records)
                if (r.endNs == 0) r.endNs = t0;
            s.records = std::move(tb->records);
            tb->records.clear();
            tb->stack.clear();
            snaps.push_back(std::move(s));
        }
    }

    // Aggregate per (module, name): self = own total - sum of children totals.
    struct Agg {
        double   selfMs  = 0.0;
        double   totalMs = 0.0;
        int      count   = 0;
        int      minDepth = 0x7fffffff;
        uint32_t module  = 0;
        std::string name;
        std::string thread;
    };
    std::unordered_map<uint64_t, Agg> aggMap;

    for (const auto& s : snaps) {
        const size_t n = s.records.size();
        std::vector<int64_t> total(n, 0);
        std::vector<int64_t> child(n, 0);
        for (size_t i = 0; i < n; ++i) {
            int64_t t = s.records[i].endNs - s.records[i].startNs;
            if (t < 0) t = 0;
            total[i] = t;
        }
        for (size_t i = 0; i < n; ++i) {
            const int32_t p = s.records[i].parent;
            if (p >= 0 && p < static_cast<int32_t>(n)) child[static_cast<size_t>(p)] += total[i];
        }
        for (size_t i = 0; i < n; ++i) {
            const auto& r     = s.records[i];
            const int64_t self = total[i] - child[i];
            Agg& a            = aggMap[zoneKey(r.module, r.name)];
            a.selfMs += static_cast<double>(self) / 1'000'000.0;
            a.totalMs += static_cast<double>(total[i]) / 1'000'000.0;
            ++a.count;
            a.module = r.module;
            a.name   = r.name ? r.name : "";
            a.thread = s.tname;
            if (r.depth < a.minDepth) a.minDepth = r.depth;
        }
    }

    Frame f;
    f.beginNs = t0;
    for (auto& kv : aggMap) {
        Agg& a = kv.second;
        ZoneSample sm;
        sm.module  = a.module < g_moduleNames.size() ? g_moduleNames[a.module]
                                                      : std::string("default");
        sm.name    = a.name;
        sm.thread  = a.thread;
        sm.selfMs  = a.selfMs;
        sm.totalMs = a.totalMs;
        sm.count   = a.count;
        sm.minDepth = a.minDepth;
        f.samples.push_back(std::move(sm));
    }
    std::sort(f.samples.begin(), f.samples.end(),
              [](const ZoneSample& x, const ZoneSample& y) { return x.selfMs > y.selfMs; });
    f.endNs = nowNs();

    lastFrameStorage() = std::move(f);
    hasFrameFlag()  = true;
    return static_cast<double>(lastFrameStorage().endNs - lastFrameStorage().beginNs) / 1'000'000.0;
}

bool Profiler::hasFrame() { return hasFrameFlag(); }

const std::vector<ZoneSample>& Profiler::lastFrame() { return lastFrameStorage().samples; }

std::string Profiler::textReport() {
    if (!hasFrameFlag()) return "=== Profiler: no frame captured yet ===\n";
    std::ostringstream os;
    char buf[64];
    const double totalMs =
        static_cast<double>(lastFrameStorage().endNs - lastFrameStorage().beginNs) / 1'000'000.0;
    std::snprintf(buf, sizeof(buf), "%.2f", totalMs);
    os << "=== Profiler frame (" << buf << " ms) ===\n";
    os << "-- Per-module / per-zone (self time, sorted) --\n";
    for (const auto& s : lastFrameStorage().samples) {
        std::snprintf(buf, sizeof(buf), "%.3f", s.selfMs);
        os << "  [" << s.module << "/" << s.thread << "] " << s.name
           << "  self=" << buf << " ms";
        std::snprintf(buf, sizeof(buf), "%.3f", s.totalMs);
        os << " total=" << buf << " ms";
        if (s.count > 1) os << " x" << s.count;
        os << "\n";
    }
    return os.str();
}

}  // namespace eve::prof
