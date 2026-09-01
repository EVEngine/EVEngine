#include "pixelworld/PixelWorld.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <mutex>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace {

class BenchmarkScheduler final : public eve::pixelworld::PixelWorkScheduler {
public:
    explicit BenchmarkScheduler(std::size_t workerCount)
        : workerCount_(std::max<std::size_t>(1, workerCount)) {
        workers_.reserve(workerCount_);
        for (std::size_t index = 0; index < workerCount_; ++index)
            workers_.emplace_back([this] { workerLoop(); });
    }

    ~BenchmarkScheduler() override {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            ++generation_;
        }
        wake_.notify_all();
    }

    void parallelFor(std::size_t workItems,
                     const std::function<void(std::size_t)>& body) override {
        if (workItems == 0) return;
        {
            std::lock_guard lock(mutex_);
            body_ = &body;
            workItems_ = workItems;
            next_.store(0, std::memory_order_relaxed);
            remaining_ = workerCount_;
            ++generation_;
        }
        wake_.notify_all();
        std::unique_lock lock(mutex_);
        complete_.wait(lock, [this] { return remaining_ == 0; });
        body_ = nullptr;
    }

    [[nodiscard]] std::size_t workerCount() const noexcept override { return workerCount_; }

private:
    void workerLoop() {
        std::size_t observedGeneration = 0;
        for (;;) {
            const std::function<void(std::size_t)>* body = nullptr;
            std::size_t workItems = 0;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this, &observedGeneration] {
                    return stopping_ || generation_ != observedGeneration;
                });
                if (stopping_) return;
                observedGeneration = generation_;
                body = body_;
                workItems = workItems_;
            }
            for (;;) {
                const std::size_t index = next_.fetch_add(1, std::memory_order_relaxed);
                if (index >= workItems) break;
                (*body)(index);
            }
            {
                std::lock_guard lock(mutex_);
                if (--remaining_ == 0) complete_.notify_one();
            }
        }
    }

    std::size_t workerCount_ = 1;
    std::vector<std::jthread> workers_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable complete_;
    const std::function<void(std::size_t)>* body_ = nullptr;
    std::atomic<std::size_t> next_ = 0;
    std::size_t workItems_ = 0;
    std::size_t remaining_ = 0;
    std::size_t generation_ = 0;
    bool stopping_ = false;
};

std::size_t parsePositive(char* text, std::size_t fallback) {
    if (text == nullptr) return fallback;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 10);
    return end != text && *end == '\0' && value > 0 ? std::size_t(value) : fallback;
}

std::uint64_t hashSnapshot(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::byte byte : bytes) {
        hash ^= std::uint8_t(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string_view benchmarkMaterial(std::string_view workload, int x, int y) noexcept {
    if (workload == "mixed-reaction") return ((x + y) & 1) != 0 ? "fire" : "water";
    if (workload == "reaction-only") return ((x + y) & 1) != 0 ? "acid" : "stone";
    return "sand";
}

}  // namespace

int main(int argc, char** argv) {
    using namespace eve::pixelworld;
    const std::size_t workers = parsePositive(argc > 1 ? argv[1] : nullptr, 4);
    const std::size_t samples = parsePositive(argc > 2 ? argv[2] : nullptr, 10);
    const std::string_view workload = argc > 3 ? std::string_view(argv[3]) : "sand";
    const bool mixedReaction = workload == "mixed-reaction";
    const bool reactionOnly = workload == "reaction-only";
    const bool thermalOnly = workload == "thermal-only";
    BenchmarkScheduler scheduler(workers);
    std::vector<double> milliseconds;
    milliseconds.reserve(samples);
    std::uint64_t totalVisited = 0;
    std::uint64_t totalMoved = 0;
    std::uint64_t totalReactions = 0;
    std::uint64_t totalThermalTransfers = 0;
    std::uint64_t totalPhaseChanges = 0;
    std::uint64_t snapshotHash = 0;
    for (std::size_t sample = 0; sample < samples; ++sample) {
        PixelWorld world(7003);
        for (int y = 0; y < 1024; ++y)
            for (int x = 0; x < 1024; ++x) {
                if (thermalOnly)
                    world.setCell(x, y, {MaterialId::Stone,
                                         std::int16_t(((x + y) & 1) != 0 ? 700 : 20), 0});
                else
                    world.setMaterial(x, y, benchmarkMaterial(workload, x, y));
            }
        const auto started = std::chrono::steady_clock::now();
        const StepStats stats = world.advanceScheduled(eve::SimulationTick(1), scheduler)
                                    .expect("million-mobile-pixel scheduled tick");
        milliseconds.push_back(std::chrono::duration<double, std::milli>(
                                   std::chrono::steady_clock::now() - started).count());
        totalVisited += stats.cellsVisited;
        totalMoved += stats.cellsMoved;
        totalReactions += stats.reactions;
        totalThermalTransfers += stats.temperatureTransfers;
        totalPhaseChanges += stats.phaseChanges;
        const auto snapshot = world.saveSnapshot().expect("million-mobile-pixel snapshot");
        const std::uint64_t sampleHash = hashSnapshot(snapshot);
        if (sample == 0)
            snapshotHash = sampleHash;
        else if (sampleHash != snapshotHash) {
            std::cerr << "non-deterministic snapshot at sample " << sample << '\n';
            return 2;
        }
    }
    std::sort(milliseconds.begin(), milliseconds.end());
    const double p50 = milliseconds[milliseconds.size() / 2];
    const std::size_t p95Index = std::min(milliseconds.size() - 1,
                                          (milliseconds.size() * 95 + 99) / 100 - 1);
    std::cout << "PIXELWORLD_ACTIVE_BENCHMARK_JSON={\"occupiedPixels\":1048576,\"mobilePixels\":"
              << (thermalOnly ? 0 : mixedReaction || reactionOnly ? 524288 : 1048576)
              << ",\"chunks\":256,\"workload\":\""
              << (mixedReaction ? "checkerboard-fire-water"
                  : reactionOnly ? "checkerboard-acid-stone"
                  : thermalOnly ? "checkerboard-hot-cold-stone" : "packed-sand-over-empty")
              << "\",\"samples\":"
              << samples << ",\"workers\":" << workers << ",\"visited\":" << totalVisited
              << ",\"moved\":" << totalMoved << ",\"snapshotHash\":" << snapshotHash
              << ",\"reactions\":" << totalReactions
              << ",\"thermalTransfers\":" << totalThermalTransfers
              << ",\"phaseChanges\":" << totalPhaseChanges
              << ",\"p50Ms\":" << p50
              << ",\"p95Ms\":" << milliseconds[p95Index] << "}\n";
    return 0;
}
