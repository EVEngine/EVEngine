#include "common/ProfilerQuery.h"

#include "common/Capability.h"
#include "common/GpuTimer.h"
#include "common/Profile.h"

#include <cstdio>
#include <string>
#include <vector>

namespace eve::profiler {
namespace {

/** JSON number with a fixed, locale-independent representation. */
void appendNumber(std::string& out, double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.4f", value);
    out += buffer;
}

void appendString(std::string& out, const std::string& value) {
    out += '"';
    for (char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char escape[8];
                    std::snprintf(escape, sizeof(escape), "\\u%04x", static_cast<unsigned>(c));
                    out += escape;
                } else {
                    out += c;
                }
                break;
        }
    }
    out += '"';
}

/**
 * @brief Read-only view over the engine-wide profiler core.
 *
 * Stateless on purpose, exactly like the decal provider: the data lives in the
 * global core (`eve::prof`), so holding a module pointer would dangle the moment
 * a host rebuilds its module set.
 */
class ProfilerQueryImpl final : public eve::IProfilerQuery {
public:
    bool enabled() const override { return eve::prof::Profiler::enabled(); }

    std::string frameJson() const override {
        const bool hasFrame = eve::prof::Profiler::hasFrame();
        auto*      timer    = eve::cap::query<eve::service::IGpuTimer>();
        const bool gpuKnown = timer != nullptr && timer->gpuTimingAvailable();

        // Depth-0 samples are the roots of each thread's zone tree, so their self
        // times are the CPU work the zones account for.
        static const std::vector<eve::prof::ZoneSample> kNoZones;
        const std::vector<eve::prof::ZoneSample>&       zones = hasFrame ? eve::prof::Profiler::lastFrame() : kNoZones;
        double                                          cpuFrameMs = 0.0;
        for (const auto& zone : zones) {
            if (zone.minDepth == 0) cpuFrameMs += zone.selfMs;
        }

        std::string out = "{\"schema\":\"eve.profiler.frame\",\"version\":1,\"enabled\":";
        out += enabled() ? "true" : "false";
        out += ",\"hasFrame\":";
        out += hasFrame ? "true" : "false";
        out += ",\"cpuFrameMs\":";
        appendNumber(out, cpuFrameMs);
        out += ",\"gpuMs\":";
        appendNumber(out, gpuKnown ? static_cast<double>(timer->gpuFrameMs()) : 0.0);
        out += ",\"gpuTimingAvailable\":";
        out += gpuKnown ? "true" : "false";
        out += ",\"zoneCount\":" + std::to_string(zones.size()) + ",\"zones\":[";
        for (std::size_t index = 0; index < zones.size(); ++index) {
            const eve::prof::ZoneSample& zone = zones[index];
            if (index) out += ',';
            out += "{\"module\":";
            appendString(out, zone.module);
            out += ",\"name\":";
            appendString(out, zone.name);
            out += ",\"thread\":";
            appendString(out, zone.thread);
            out += ",\"selfMs\":";
            appendNumber(out, zone.selfMs);
            out += ",\"totalMs\":";
            appendNumber(out, zone.totalMs);
            out += ",\"count\":" + std::to_string(zone.count) + ",\"depth\":" + std::to_string(zone.minDepth) + "}";
        }
        out += "]}";
        return out;
    }

    std::string textReport() const override {
        std::string report = eve::prof::Profiler::textReport();
        auto*       timer  = eve::cap::query<eve::service::IGpuTimer>();
        if (timer && timer->gpuTimingAvailable()) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "  GPU frame: %.2f ms\n", timer->gpuFrameMs());
            report += buffer;
        }
        return report;
    }
};

}  // namespace

void registerProfilerCapabilities() {
    static ProfilerQueryImpl impl;
    eve::cap::provide<eve::IProfilerQuery>(&impl);
}

}  // namespace eve::profiler
