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
struct CompiledProgram {
    std::vector<uint32_t> words;
    double                milliseconds = 0;
};
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
    explicit CompilerQueue(uint32_t count) {
        try {
            for (uint32_t i = 0; i < count; ++i)
                workers.emplace_back([this] {
                    for (;;) {
                        Job job;
                        {
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
                        job();  // packaged_task transports failures to the owning device thread.
                        --running;
                        {
                            std::lock_guard lock(mutex);
                            --inFlight;
                        }
                        ready.notify_all();
                    }
                });
        } catch (...) {
            stop();
            throw;
        }
    }
    ~CompilerQueue() { stop(); }
    // Called on the device thread. Run bounds each segment to 48 operations;
    // this independent queue bound also protects accidental future callers.
    std::future<CompiledProgram> enqueue(std::string source) {
        Job  job([source = std::move(source)] {
            const auto start  = std::chrono::steady_clock::now();
            auto       result = gpgpu::compileComputeSpirv(source);
            if (!result.ok()) throw std::runtime_error(result.error()->message() + "\nCompute source:\n" + source);
            return CompiledProgram{
                std::move(result.value()),
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()};
        });
        auto future = job.get_future();
        {
            std::lock_guard lock(mutex);
            if (stopping || jobs.size() >= 48) throw std::runtime_error("ONNX compiler queue unavailable");
            jobs.push_back(std::move(job));
        }
        ready.notify_one();
        return future;
    }
    size_t peakWorkers() const noexcept { return peak.load(); }
    void   waitIdle() noexcept {
        std::unique_lock lock(mutex);
        ready.wait(lock, [this] { return jobs.empty() && inFlight == 0; });
    }
    void resetPeak() noexcept { peak = 0; }
};
}  // namespace eve::tensor::onnx_detail
