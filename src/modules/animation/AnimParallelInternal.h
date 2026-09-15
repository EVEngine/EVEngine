#pragma once

#include <algorithm>
#include <atomic>
#include <exception>
#include <thread>
#include <vector>

namespace eve::animation::detail {
// Synchronous, bounded fork/join for disjoint native animation work. Nothing
// survives the call, including when thread creation or an item throws.
template <class Body>
int parallelAnimationItems(std::size_t count, int requested, Body&& body) {
    if (count == 0) return 0;
    const auto                      limit   = requested > 0 ? static_cast<unsigned>(requested) : 8u;
    const auto                      workers = std::min({count, static_cast<std::size_t>(limit),
                                                        static_cast<std::size_t>(std::max(1u, std::thread::hardware_concurrency()))});
    std::vector<std::exception_ptr> errors(count);
    std::atomic<std::size_t>        next{0};
    auto                            run = [&] {
        for (;;) {
            const auto index = next.fetch_add(1, std::memory_order_relaxed);
            if (index >= count) return;
            try {
                body(index);
            } catch (...) {
                errors[index] = std::current_exception();
            }
        }
    };
    std::vector<std::thread> threads;
    threads.reserve(workers - 1);
    try {
        for (std::size_t i = 1; i < workers; ++i) threads.emplace_back(run);
    } catch (...) {
        for (auto& thread : threads) thread.join();
        throw;
    }
    run();
    for (auto& thread : threads) thread.join();
    // Report the first failing input, independent of completion order.
    for (const auto& error : errors)
        if (error) std::rethrow_exception(error);
    return static_cast<int>(workers);
}
}  // namespace eve::animation::detail
