#include "editor/VegetationConverterController.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve;
using namespace eve::asset_import;
using namespace eve::editor;

namespace {
class Preparer final : public IVegetationConversionPreparer {
public:
    Result<PreparedAssetImport> prepare(const UnityVegetationBatchImportRequest&) const override {
        if (reject)
            return Result<PreparedAssetImport>::failure(
                Diagnostic::error(DiagnosticCode::ParseError, "injected prepare failure"));
        PreparedAssetImport result;
        for (const char* uri :
             {"asset://550e8400-e29b-41d4-a716-446655440001", "asset://550e8400-e29b-41d4-a716-446655440002",
              "asset://550e8400-e29b-41d4-a716-446655440003"}) {
            auto reference = AssetRef::parse(uri);
            REQUIRE(reference.ok());
            result.manifest.assets.push_back(
                {std::move(reference).takeValue(), "eve.test", SchemaVersion(1), "asset.json", "sha256:test", {}});
        }
        result.findings.resize(2);
        return Result<PreparedAssetImport>::success(std::move(result));
    }
    bool reject = false;
};

class Publisher final : public IVegetationConversionPublisher {
public:
    EditorResult<std::uint64_t> publish(const PreparedAssetImport& candidate,
                                        std::uint64_t              expectedGeneration) override {
        ++calls;
        observedAssets = candidate.manifest.assets.size();
        if (reject)
            return editing::failed<std::uint64_t>(editing::Status::Conflict, editing::RuleId("test.converter.publish"),
                                                  "injected publish conflict");
        return editing::applied<std::uint64_t>(expectedGeneration + 1);
    }
    bool        reject         = false;
    int         calls          = 0;
    std::size_t observedAssets = 0;
};

UnityVegetationBatchImportRequest request(const char* name) {
    UnityVegetationBatchImportRequest result;
    result.package.packageId      = *PersistentId::parse("11111111-2222-4333-8444-555555555555");
    result.package.packageName    = name;
    result.package.packageVersion = "1";
    result.objects.emplace_back();
    return result;
}
}  // namespace

TEST_CASE("editor.vegetation_converter_prepares_and_publishes_revision_bound_candidate") {
    Preparer                      preparer;
    Publisher                     publisher;
    VegetationConverterController controller(preparer, publisher);
    REQUIRE(controller.setRequest(request("forest"), 7).ok());
    REQUIRE(controller.prepare().ok());
    auto state = controller.state();
    CHECK_EQ(static_cast<int>(state.phase), static_cast<int>(VegetationConverterState::Phase::Prepared));
    CHECK_EQ(state.assetCount, 3U);
    CHECK_EQ(state.findingCount, 2U);
    auto published = controller.publish();
    REQUIRE(published.ok());
    CHECK_EQ(published.value(), 8U);
    CHECK_EQ(publisher.calls, 1);
    CHECK_EQ(publisher.observedAssets, 3U);
}

TEST_CASE("editor.vegetation_converter_invalidates_old_candidate_and_retains_retry_on_failure") {
    Preparer                      preparer;
    Publisher                     publisher;
    VegetationConverterController controller(preparer, publisher);
    REQUIRE(controller.setRequest(request("forest"), 2).ok());
    REQUIRE(controller.prepare().ok());
    REQUIRE(controller.setRequest(request("meadow"), 2).ok());
    CHECK(!controller.publish().ok());
    publisher.reject = true;
    REQUIRE(controller.prepare().ok());
    CHECK(!controller.publish().ok());
    CHECK_EQ(publisher.calls, 1);
    publisher.reject = false;
    auto retried     = controller.publish();
    REQUIRE(retried.ok());
    CHECK_EQ(retried.value(), 3U);
}

TEST_CASE("editor.vegetation_converter_surfaces_prepare_diagnostics_without_candidate") {
    Preparer preparer;
    preparer.reject = true;
    Publisher                     publisher;
    VegetationConverterController controller(preparer, publisher);
    REQUIRE(controller.setRequest(request("forest"), 0).ok());
    CHECK(!controller.prepare().ok());
    const auto state = controller.state();
    CHECK_EQ(static_cast<int>(state.phase), static_cast<int>(VegetationConverterState::Phase::Error));
    CHECK(!state.diagnostics.empty());
    CHECK(!controller.publish().ok());
    CHECK_EQ(publisher.calls, 0);
}
