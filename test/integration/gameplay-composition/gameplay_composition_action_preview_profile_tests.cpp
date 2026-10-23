#include "action/editor/ActionPreviewProfile.h"

#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <map>
#include <filesystem>

namespace {

eve::action::ActionTimeline previewTimeline() {
    eve::action::ActionTimeline timeline;
    timeline.actionId = *eve::LogicalId::parse("combat:preview-test");
    timeline.duration = eve::Duration::fromSeconds(2.0).takeValue();
    eve::action::ActionAnimationSection first;
    first.id           = *eve::LogicalId::parse("animation:first");
    first.animationUri = "asset://animations/first";
    first.end          = eve::Duration::fromSeconds(1.0).takeValue();
    eve::action::ActionAnimationSection second;
    second.id           = *eve::LogicalId::parse("animation:second");
    second.animationUri = "asset://animations/second";
    second.start        = first.end;
    second.end          = timeline.duration;
    timeline.animationSections = {first, second};
    return timeline;
}

eve::editor::ActionPreviewSkeleton skeleton(std::string uri, std::string root = "Root",
                                            std::string child = "Spine") {
    return {std::move(uri), {{std::move(root), -1}, {std::move(child), 0}}};
}

class PreviewAssets final : public eve::editor::IActionPreviewAssetProvider {
public:
    std::map<std::string, eve::editor::ActionPreviewSkeleton> models;
    std::map<std::string, eve::editor::ActionPreviewSkeleton> animations;
    bool                                                       acceptMask = true;

    eve::Result<eve::editor::ActionPreviewSkeleton> modelSkeleton(std::string_view uri) override {
        auto found = models.find(std::string(uri));
        if (found == models.end())
            return eve::Result<eve::editor::ActionPreviewSkeleton>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "preview model was not found", "previewModelUri"));
        return eve::Result<eve::editor::ActionPreviewSkeleton>::success(found->second);
    }

    eve::Result<eve::editor::ActionPreviewSkeleton> animationSkeleton(std::string_view uri) override {
        auto found = animations.find(std::string(uri));
        if (found == animations.end())
            return eve::Result<eve::editor::ActionPreviewSkeleton>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::NotFound, "animation was not found", "animationUri"));
        return eve::Result<eve::editor::ActionPreviewSkeleton>::success(found->second);
    }

    eve::Result<void> validateBoneMask(std::string_view,
                                       const eve::editor::ActionPreviewSkeleton&) override {
        if (!acceptMask)
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "mask targets another skeleton", "boneMaskUri"));
        return eve::Result<void>::success();
    }
};

eve::editor::ActionPreviewProfile previewProfile() {
    eve::editor::ActionPreviewProfile profile;
    profile.actionAsset          = eve::editor::AssetGuid("asset-preview-test");
    profile.previewModelUri      = "asset://models/hero";
    profile.baseMotionUri        = "asset://animations/idle";
    profile.animationProfileUri  = "asset://animation-profiles/humanoid";
    profile.boneMaskUri          = "asset://animation-masks/upper-body";
    profile.showSkeleton         = true;
    profile.showJointNames       = true;
    profile.shading              = eve::editor::ActionPreviewShading::Unlit;
    return profile;
}

}  // namespace

TEST_CASE("actionPreviewProfile.versionedSidecarRoundTripsAndRejectsUnknownVersion") {
    auto profile = previewProfile();
    auto encoded = profile.toValue();
    REQUIRE(encoded.ok());
    auto decoded = eve::editor::ActionPreviewProfile::fromValue(encoded.value());
    REQUIRE(decoded.ok());
    CHECK(decoded.value() == profile);

    eve::Value unknown = encoded.value();
    unknown.set("schemaVersion", static_cast<std::int64_t>(2));
    auto rejected = eve::editor::ActionPreviewProfile::fromValue(unknown);
    CHECK(!rejected.ok());
}

TEST_CASE("actionPreviewProfile.compatibilityIsExplicitForExactRetargetedAndUnavailableProviders") {
    auto timeline = previewTimeline();
    auto profile  = previewProfile();
    PreviewAssets assets;
    assets.models.emplace(profile.previewModelUri, skeleton(profile.previewModelUri));
    assets.animations.emplace("asset://animations/first", skeleton("asset://animations/first"));
    assets.animations.emplace("asset://animations/second", skeleton("asset://animations/second", "root", "spine"));

    eve::editor::ActionPreviewCompatibilityAnalyzer analyzer;
    auto report = analyzer.analyze(timeline, profile, &assets);
    REQUIRE(report.ok());
    REQUIRE(report.value().sections.size() == 2);
    CHECK(report.value().sections[0].compatibility == eve::editor::ActionPreviewCompatibility::Compatible);
    CHECK(report.value().sections[1].compatibility == eve::editor::ActionPreviewCompatibility::Retargetable);
    CHECK(report.value().canPreview());

    auto unsupported = analyzer.analyze(timeline, profile, nullptr);
    REQUIRE(unsupported.ok());
    CHECK(!unsupported.value().canPreview());
    CHECK(unsupported.value().sections[0].compatibility == eve::editor::ActionPreviewCompatibility::Unsupported);

    assets.acceptMask = false;
    auto badMask = analyzer.analyze(timeline, profile, &assets);
    REQUIRE(badMask.ok());
    CHECK(!badMask.value().canPreview());
}

TEST_CASE("actionPreviewProfile.sidecarStoreRoundTripsAndRejectsIdentityMismatch") {
    const auto root = std::filesystem::temp_directory_path() / "eve-action-preview-profile-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    eve::editor::ActionPreviewProfileStore store(root);
    auto profile = previewProfile();
    REQUIRE(store.save(profile).ok());
    auto loaded = store.load(profile.actionAsset);
    REQUIRE(loaded.ok());
    CHECK(loaded.value() == profile);
    auto missing = store.load(eve::editor::AssetGuid("missing-action"));
    CHECK(!missing.ok());
    std::filesystem::remove_all(root, error);
}
