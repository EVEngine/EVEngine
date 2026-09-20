#include "editor/VegetationConverterController.h"

namespace eve::editor {
namespace {
template <class T>
EditorResult<T> fail(EditorStatus status, const char* rule, std::string message) {
    return editing::failed<T>(status, RuleId(rule), std::move(message));
}

std::vector<EditorDiagnostic> project(const std::vector<Diagnostic>& diagnostics) { return diagnostics; }
}  // namespace

Result<asset_import::PreparedAssetImport> NativeVegetationConversionPreparer::prepare(
    const asset_import::UnityVegetationBatchImportRequest& request) const {
    return asset_import::prepareUnityVegetationConversionBatch(request);
}

VegetationConverterController::VegetationConverterController(const IVegetationConversionPreparer& preparer,
                                                             IVegetationConversionPublisher&      publisher)
    : preparer_(preparer), publisher_(publisher) {}

EditorResult<void> VegetationConverterController::setRequest(asset_import::UnityVegetationBatchImportRequest request,
                                                             std::uint64_t expectedPublicationGeneration) {
    if (request.package.packageId.isNil() || request.package.packageName.empty() ||
        request.package.packageVersion.empty() || request.objects.empty())
        return fail<void>(EditorStatus::Rejected, "editor.vegetation-converter.request",
                          "Vegetation conversion requires package identity and at least one object");
    request_ = std::move(request);
    candidate_.reset();
    expectedPublicationGeneration_ = expectedPublicationGeneration;
    ++state_.requestRevision;
    state_.preparedRevision      = 0;
    state_.publicationGeneration = 0;
    state_.objectCount           = request_->objects.size();
    state_.assetCount            = 0;
    state_.findingCount          = 0;
    state_.diagnostics.clear();
    state_.phase = VegetationConverterState::Phase::Dirty;
    return editing::applied<void>();
}

EditorResult<void> VegetationConverterController::prepare() {
    if (!request_)
        return fail<void>(EditorStatus::Rejected, "editor.vegetation-converter.no-request",
                          "Set a vegetation conversion request before preparing it");
    auto prepared = preparer_.prepare(*request_);
    if (!prepared.ok()) {
        candidate_.reset();
        state_.preparedRevision = 0;
        state_.assetCount       = 0;
        state_.findingCount     = 0;
        state_.diagnostics      = project(prepared.diagnostics());
        state_.phase            = VegetationConverterState::Phase::Error;
        return EditorResult<void>::failure(prepared.status());
    }
    candidate_              = std::move(prepared).takeValue();
    state_.preparedRevision = state_.requestRevision;
    state_.assetCount       = candidate_->manifest.assets.size();
    state_.findingCount     = candidate_->findings.size();
    state_.diagnostics.clear();
    state_.phase = VegetationConverterState::Phase::Prepared;
    return editing::applied<void>();
}

EditorResult<std::uint64_t> VegetationConverterController::publish() {
    if (!candidate_ || state_.preparedRevision != state_.requestRevision)
        return fail<std::uint64_t>(EditorStatus::Conflict, "editor.vegetation-converter.stale-candidate",
                                   "Prepare the current vegetation request before publishing it");
    auto published = publisher_.publish(*candidate_, expectedPublicationGeneration_);
    if (!published.ok()) {
        state_.diagnostics = published.diagnostics();
        state_.phase       = VegetationConverterState::Phase::Error;
        return published;
    }
    expectedPublicationGeneration_ = published.value();
    state_.publicationGeneration   = published.value();
    state_.diagnostics.clear();
    state_.phase = VegetationConverterState::Phase::Published;
    return published;
}

EditorResult<void> VegetationConverterController::clear() {
    request_.reset();
    candidate_.reset();
    state_                         = {};
    expectedPublicationGeneration_ = 0;
    return editing::applied<void>();
}

VegetationConverterState VegetationConverterController::state() const { return state_; }

}  // namespace eve::editor
