#include "common/ArtifactPublication.h"

namespace eve::artifact {

eve::Result<eve::Value> ProviderContract::snapshotState() const {
    return eve::Result<eve::Value>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                                                   "artifact provider does not expose snapshot state",
                                                                   "artifact.provider.snapshot"));
}

eve::Result<void> ProviderContract::restoreState(const eve::Value&) {
    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Unsupported,
                                                             "artifact provider does not support snapshot restore",
                                                             "artifact.provider.restore"));
}

}  // namespace eve::artifact
