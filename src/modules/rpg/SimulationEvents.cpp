#include "rpg/SimulationEvents.h"

namespace eve::rpg::detail {
namespace {
thread_local SimulationEvents defaults;
// Internal observer: active nested owners outlive the synchronous scope that selects them.
thread_local SimulationEvents* current = &defaults;
}  // namespace
SimulationEvents& simulationEvents() { return *current; }
SimulationEventScope::SimulationEventScope(SimulationEvents& next) : previous_(*current) { current = &next; }
SimulationEventScope::~SimulationEventScope() { current = &previous_; }
}  // namespace eve::rpg::detail
