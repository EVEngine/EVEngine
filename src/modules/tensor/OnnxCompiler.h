#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
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
    /** @brief Enqueue. */
    std::future<CompiledProgram> enqueue(std::string source) {
        /** @brief Job. */
        Job  job([source = std::move(source)] {
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
    }
    /** @brief Resets peak. */
    void resetPeak() noexcept { peak = 0; }
};
}  // namespace eve::tensor::onnx_detail
