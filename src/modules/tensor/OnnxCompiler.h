#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include "gpgpu/ComputeProgram.h"

namespace eve::tensor::onnx_detail {
// Session-owned CPU workers. Jobs own their source and never capture Graphics,
// Run, buffers or callbacks. Joining the workers needs no device access.
/** @brief CompiledProgram public API. */
struct CompiledProgram {
    std::vector<uint32_t> words;
    double                milliseconds = 0;
};
/** @brief CompilerQueue public API. */
class CompilerQueue {
    using Job = std::packaged_task<CompiledProgram()>;
    // Keep the first bounded segment's timestamps in memory. Worker-side I/O
    // would perturb precisely the overlap that a low-peak failure needs to explain.
    struct Timing {
        long long              enqueued = 0;
        std::atomic<long long> started{0}, finished{0};
    };
    std::array<Timing, 48> timings;
    size_t                 timingCount = 0;
    static long long       timestamp() noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    std::mutex               mutex;
    std::condition_variable  ready;
    std::deque<Job>          jobs;
    bool                     stopping = false;
    size_t                   inFlight = 0;
    std::vector<std::thread> workers;
    std::atomic<size_t>      running{0}, peak{0};
    void                     stop() noexcept {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        ready.notify_all();
        for (auto& worker : workers)
            if (worker.joinable()) worker.join();
    }

public:
    /** @brief Constructs a CompilerQueue. */
    explicit CompilerQueue(uint32_t count) {
        try {
            for (uint32_t i = 0; i < count; ++i)
                workers.emplace_back([this] {
                    for (;;) {
                        Job job;
                        {
                            /** @brief Locks lock. */
                            std::unique_lock lock(mutex);
                            ready.wait(lock, [this] { return stopping || !jobs.empty(); });
                            if (jobs.empty()) return;
                            job = std::move(jobs.front());
                            jobs.pop_front();
                            ++inFlight;
                        }
                        const auto n = ++running;
                        auto       p = peak.load();
                        while (p < n && !peak.compare_exchange_weak(p, n)) {
                        }
                        /** @brief Job. */
                        job();  // packaged_task transports failures to the owning device thread.
                        --running;
                        {
                            /** @brief Locks lock. */
                            std::lock_guard lock(mutex);
                            --inFlight;
                        }
                        ready.notify_all();
                    }
                });
        } catch (...) {
            /** @brief Stops stop. */
            stop();
            throw;
        }
    }
    /** @brief Releases CompilerQueue resources. */
    ~CompilerQueue() { stop(); }
    // Called on the device thread. Run bounds each segment to 48 operations;
    // this independent queue bound also protects accidental future callers.
    /** @brief Enqueue CPU-only shader compilation.
     * @lifetime Captured timing slots belong to this queue. stop() joins every
     * worker before their destruction; resetPeak() follows the prior waitIdle().
     * @thread Called by the owning device thread, like waitIdle() and resetPeak().
     */
    std::future<CompiledProgram> enqueue(std::string source) {
        auto* timing = timingCount < timings.size() ? &timings[timingCount] : nullptr;
        ++timingCount;
        if (timing) timing->enqueued = timestamp();
        /** @brief Job. */
        Job  job([timing, source = std::move(source)] {
            if (timing) timing->started = timestamp();
            struct Completion {
                Timing* timing;
                ~Completion() {
                    if (timing) timing->finished = timestamp();
                }
            } completion{timing};
            const auto start  = std::chrono::steady_clock::now();
            auto       result = gpgpu::compileComputeSpirv(source);
            if (!result.ok()) throw std::runtime_error(result.error()->message() + "\nCompute source:\n" + source);
            return CompiledProgram{
                /** @brief Moves move. */
                std::move(result.value()),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()};
        });
        auto future = job.get_future();
        {
            /** @brief Locks lock. */
            std::lock_guard lock(mutex);
            if (stopping || jobs.size() >= 48) throw std::runtime_error("ONNX compiler queue unavailable");
            jobs.push_back(std::move(job));
        }
        ready.notify_one();
        return future;
    }
    /** @brief Peak workers. */
    size_t peakWorkers() const noexcept { return peak.load(); }
    /** @brief Waits idle. */
    void   waitIdle() noexcept {
        /** @brief Locks lock. */
        std::unique_lock lock(mutex);
        ready.wait(lock, [this] { return jobs.empty() && inFlight == 0; });
        // Report only after the workers are idle, without changing the peak or
        // waiting for overlap. This also covers failed/abandoned graph cleanup.
        if (workers.size() > 1 && timingCount > 1 && peak.load() == 1) {
            const auto count  = std::min(timingCount, timings.size());
            const auto origin = timings[0].enqueued;
            for (size_t i = 0; i < count; ++i) {
                const auto& t = timings[i];
                std::fprintf(
                    stderr,
                    "ONNX_COMPILER_TIMING job=%zu workers=%zu peak=1 enqueue_us=%lld start_us=%lld end_us=%lld\n", i,
                    workers.size(), t.enqueued - origin, t.started.load() - origin, t.finished.load() - origin);
            }
        }
    }
    /** @brief Resets peak. */
    void resetPeak() noexcept {
        peak        = 0;
        timingCount = 0;
        for (auto& timing : timings) {
            timing.enqueued = 0;
            timing.started  = 0;
            timing.finished = 0;
        }
    }
};
}  // namespace eve::tensor::onnx_detail
