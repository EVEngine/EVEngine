#include "common/CrashHandler.h"
#include "crowd/Crowd.h"
#include "crowd/CrowdWorkScheduler.h"
#if defined(EV_CROWD_BENCH_THREAD)
#include "crowd/thread/CrowdThread.h"
#include "thread/JobSystem.h"
#endif

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
bool positiveInteger(std::string_view text, int& value) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value > 0;
}
double percentile(const std::vector<double>& sorted, double fraction) {
    return sorted[std::min(sorted.size() - 1,
                           static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size())) - 1.0))];
}
}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    eve::installCrashHandler();
#endif
    int count = 1000, frames = 3600, workers = 1;
    if (argc > 4 || (argc > 1 && !positiveInteger(argv[1], count)) || (argc > 2 && !positiveInteger(argv[2], frames)) ||
        (argc > 3 && !positiveInteger(argv[3], workers)) || (workers != 1 && workers != 4) ||
        count > 100000 || frames > 36000) {
        std::cerr << "usage: eve_crowd_benchmark [agents:1..100000] [frames:1..36000] [workers:1|4]\n";
        return 2;
    }
    eve::crowd::Crowd crowd;
#if defined(EV_CROWD_BENCH_THREAD)
    auto jobs = std::shared_ptr<eve::thread::JobSystem>(eve::thread::createJobSystem(workers));
    if (workers == 4) {
        auto scheduler = eve::crowd_thread::makeJobSystemCrowdScheduler(jobs);
        if (!scheduler || !crowd.setWorkScheduler(eve::crowd::CrowdExecution::Parallel, scheduler.value())) return 1;
    }
#else
    if (workers != 1) return 2;
#endif
    crowd.setMaxAgents(count);
    crowd.setClampToField(false);
    crowd.setDefaultSpeed(2.f);
    crowd.setSeparationWeight(0.f);
    crowd.setArriveRadius(0.5f);
    auto configured = crowd.configureAvoidance({true, 2.f, 0.1f, 32});
    if (!configured.ok()) {
        std::cerr << "avoidance configuration rejected\n";
        return 1;
    }
    const int       columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
    constexpr float spacing = 1.5f, dt = 1.f / 60.f;
    for (int index = 0; index < count; ++index) {
        const float x = float(index % columns) * spacing, y = float(index / columns) * spacing;
        const int   slot = crowd.addAgent(x, y, 0.f, 0.5f);
        if (slot < 0 || !crowd.setAgentAction(slot, "seek") || !crowd.setAgentTarget(slot, x + 10000.f, y)) {
            std::cerr << "agent setup rejected\n";
            return 1;
        }
    }
    constexpr int warmup = 120;
    for (int frame = 0; frame < warmup; ++frame) {
        auto result = crowd.advance(dt);
        if (!result.ok()) {
            std::cerr << "warmup advance rejected\n";
            return 1;
        }
    }
    std::vector<double> elapsed;
    elapsed.reserve(static_cast<std::size_t>(frames));
    std::cout << "# path=core-motion,scene=parallel-march,layout=deterministic-grid,seed=none"
              << ",agents=" << count << ",spacing=" << spacing << ",radius=0.5,speed=2,dt=" << dt
              << ",warmup=" << warmup << ",frames=" << frames
              << ",avoidance=velocity-sampling,maxNeighbors=32,horizon=2,margin=0.1,workers=1"
              << ",separationRadius=28,perceptionRadius=64,resolveOverlaps=true,maxContactPasses=12\n";
    std::cout << "frame,total_ns,grid_ns,motion_ns,contact_ns,wall_ns,residual_ns,grid_builds,peak_cells,"
                 "cells_visited,candidates_visited,pairs_checked,contact_passes,predictions,truncations,"
                 "unresolved_contacts,max_penetration,peak_entries,sort_comparisons,spatial_visits\n";
    for (int frame = 0; frame < frames; ++frame) {
        const auto start  = std::chrono::steady_clock::now();
        auto       result = crowd.advance(dt);
        const auto ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count();
        if (!result.ok()) {
            std::cerr << "measured advance rejected at frame " << frame << '\n';
            return 1;
        }
        const auto& report = result.value();
        elapsed.push_back(static_cast<double>(ns));
        std::cout << frame << ',' << ns << ',' << report.gridNanoseconds << ',' << report.motionNanoseconds << ','
                  << report.contactNanoseconds << ',' << report.wallNanoseconds << ',' << report.residualNanoseconds
                  << ',' << report.gridBuilds << ',' << report.gridCellsPeak << ',' << report.cellsVisited << ','
                  << report.candidatesVisited << ',' << report.contactPairsChecked << ',' << report.contactPasses << ','
                  << report.avoidanceChecks << ',' << report.avoidanceTruncations << ',' << report.unresolvedContacts
                  << ',' << report.maxPenetration << ',' << report.gridEntriesPeak << ',' << report.gridSortComparisons
                  << ',' << report.spatialVisits << '\n';
    }
    std::sort(elapsed.begin(), elapsed.end());
    std::cout << "# total_ms_p50=" << percentile(elapsed, 0.50) / 1000000.0
              << ",p95=" << percentile(elapsed, 0.95) / 1000000.0 << ",p99=" << percentile(elapsed, 0.99) / 1000000.0
              << '\n';
    return 0;
}
