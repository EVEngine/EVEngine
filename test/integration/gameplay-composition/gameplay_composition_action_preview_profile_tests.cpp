// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "module": "action_editor",
  "modules": [
    "action",
    "action_editor",
    "animation"
  ],
  "origins": [
    "action_preview_profile.cpp"
  ]
}
*/
// clang-format on

#include "action/editor/ActionPreviewProfile.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <filesystem>

namespace {
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
