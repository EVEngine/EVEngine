// Domain facades are checked for standalone include hygiene.  No module target
// or unit-test runner is linked here; this catches accidental transitive-header
// dependencies before a profile attempts a real build.
#include "action/Action.h"
#include "physics/PhysicsHandles.h"
#include "physics/PhysicsLink.h"
#include "physics/SimulationBackend.h"
#include "physics/World.h"
#include "procgen/core/ProcgenCore.h"
#include "settlement/Settlement.h"

namespace eve::profile_check {
void domainPublicHeadersCompile() {}
}  // namespace eve::profile_check
