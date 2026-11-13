// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "compile-contract",
  "default": "module-composition",
  "module": "common"
}
*/
// clang-format on

// Each public header group is compiled in its own translation unit.  This is
// intentionally outside test/CMakeLists.txt: it is a compile contract, not a
// zeroerr case and must remain usable without the full unit-test executable.
// A consumer may include Windows/SDL headers without NOMINMAX before this API.
// Preload the standard dependencies so the probe isolates our header, and leave
// the caller's macro environment unchanged after checking sentinel semantics.
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#pragma push_macro("max")
#undef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#include "common/RuntimeHandle.h"
namespace {
struct MacroHandleTag;
using MacroHandle = eve::RuntimeHandle<MacroHandleTag>;
static_assert(MacroHandle::invalidIndex == 0xffffffffu);
static_assert(!MacroHandle::nextGeneration(0xffffffffu).has_value());
static_assert(MacroHandle::nextGeneration(1u).value() == 2u);
static_assert(max(1, 2) == 2);
}  // namespace
#pragma pop_macro("max")

#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/EventSequence.h"
#include "common/FrameCapture.h"
#include "common/FrameCaptureImage.h"
#include "common/Generation.h"
#include "common/Identity.h"
#include "common/Json.h"
#include "common/ResourceAccount.h"
#include "common/ResourceRef.h"
#include "common/Result.h"
#include "common/Revision.h"
#include "common/RuntimeHandle.h"
#include "common/SchemaVersion.h"
#include "common/Snapshot.h"
#include "common/Status.h"
#include "common/StrongUint64.h"
#include "common/SubjectRef.h"
#include "common/Subscription.h"
#include "common/Time.h"
#include "common/Uri.h"
#include "common/UuidV7.h"
#include "common/Value.h"
#include "common/VersionedRegistry.h"

namespace eve::profile_check {
void commonPublicHeadersCompile() {}
}  // namespace eve::profile_check
