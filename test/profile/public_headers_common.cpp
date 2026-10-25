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
