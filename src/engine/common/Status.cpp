#include "common/Status.h"
namespace eve {
const std::vector<Diagnostic>& Status::emptyDiagnostics() noexcept {
    static const std::vector<Diagnostic> empty(0);
    return empty;
}
namespace {
// Init-on-use is safe for other static observers; library startup warms the read-only observation before commits.
[[maybe_unused]] const bool emptyObservationReady = [] {
    static_cast<void>(Status::success().diagnostics());
    return true;
}();
}  // namespace
}  // namespace eve
