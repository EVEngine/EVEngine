#include "procgen/ProcgenLive.h"

#include "procgen/Procgen.h"

#include <atomic>

namespace eve::procgen {
namespace {

std::atomic<Procgen*> g_liveProcgen{nullptr};

}  // namespace

Procgen* liveProcgen() noexcept { return g_liveProcgen.load(std::memory_order_acquire); }

void publishLiveProcgen(Procgen* instance) noexcept {
    g_liveProcgen.store(instance, std::memory_order_release);
}

void clearLiveProcgen(Procgen* instance) noexcept {
    Procgen* expected = instance;
    (void)g_liveProcgen.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel,
                                                std::memory_order_acquire);
}

}  // namespace eve::procgen
