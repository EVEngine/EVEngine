// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"devtools"}
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <algorithm>
#include <filesystem>
#include "common/ActionGuidePack.h"
#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "devtools/ActionGuideExport.hpp"
#include "devtools/McpActionGuideTools.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
Poco::JSON::Object::Ptr guideResult(const std::string& response) {
    Poco::JSON::Parser parser;
    auto               envelope = parser.parse(response).extract<Poco::JSON::Object::Ptr>();
    return parser.parse(envelope->getArray("content")->getObject(0)->getValue<std::string>("text"))
        .extract<Poco::JSON::Object::Ptr>();
}
}  // namespace

TEST_CASE("module_composition.actionGuide.fakeCaptureExportsArtifactLayout") {
    using namespace eve;
    struct Fake final : IActionGuideCapture {
        ActionGuideCaptureSnapshot snap;
        Result<std::string> requestCapture(std::string_view packJson, std::span<const std::string> shotIds) override {
            auto parsed = Value::fromJson(std::string(packJson));
            if (!parsed) return Result<std::string>::failure(parsed.status());
            auto pack = decodeActionGuidePack(parsed.value());
            if (!pack) return Result<std::string>::failure(pack.status());
            snap                = {};
            snap.token          = "composition-guide-1";
            snap.phase          = ActionGuideCapturePhase::Ready;
            snap.revision       = pack.value().revision;
            snap.backend        = "composition-fake";
            snap.pack           = pack.value();
            const uint32_t w    = static_cast<uint32_t>(pack.value().canvas.width);
            const uint32_t h    = static_cast<uint32_t>(pack.value().canvas.height);
            auto           rgba = std::make_shared<std::vector<uint8_t>>(size_t(w) * h * 4, uint8_t{0});
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    const size_t i = (size_t(y) * w + x) * 4;
                    const bool   inside =
                        (int(x) - int(w) / 2) * (int(x) - int(w) / 2) + (int(y) - int(h) / 2) * (int(y) - int(h) / 2) <
                        int(w * h / 16);
                    if (inside) {
                        (*rgba)[i + 0] = (*rgba)[i + 1] = (*rgba)[i + 2] = (*rgba)[i + 3] = 255;
                    }
                }
            }
            auto depth = std::make_shared<std::vector<uint8_t>>(size_t(w) * h * 4, uint8_t{0});
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    const size_t i  = (size_t(y) * w + x) * 4;
                    const auto   g  = static_cast<uint8_t>((x * 255u) / (w ? w : 1u));
                    (*depth)[i + 0] = (*depth)[i + 1] = (*depth)[i + 2] = g;
                    (*depth)[i + 3]                                     = 255;
                }
            }
            for (const auto& shot : pack.value().shots) {
                if (!shotIds.empty() && std::find(shotIds.begin(), shotIds.end(), shot.shotId) == shotIds.end())
                    continue;
                ActionGuideShotArtifact artifact;
                artifact.shotId      = shot.shotId;
                artifact.timeSeconds = shot.timeSeconds;
                artifact.sampleIndex = shot.sampleIndex;
                artifact.view        = shot.view;
                artifact.tags        = shot.tags;
                for (const auto& aov : shot.aovs) {
                    if (aov != "silhouette" && aov != "depth") continue;
                    artifact.aovs.push_back(ActionGuideAovBuffer{aov, w, h, aov == "silhouette" ? rgba : depth});
                }
                snap.shots.push_back(std::move(artifact));
            }
            return Result<std::string>::success(snap.token);
        }
        Result<ActionGuideCaptureSnapshot> captureSnapshot(std::string_view token) override {
            if (token != snap.token)
                return Result<ActionGuideCaptureSnapshot>::failure(
                    Diagnostic::error(DiagnosticCode::NotFound, "unknown token"));
            return Result<ActionGuideCaptureSnapshot>::success(snap);
        }
        Result<void> releaseCapture(std::string_view token) override {
            if (token != snap.token)
                return Result<void>::failure(Diagnostic::error(DiagnosticCode::NotFound, "unknown token"));
            snap = {};
            return Result<void>::success();
        }
    } fake;
    struct Registration {
        IActionGuideCapture* previous = cap::query<IActionGuideCapture>();
        ~Registration() { cap::provide<IActionGuideCapture>(previous); }
    } registration;

    cap::provide<IActionGuideCapture>(&fake);
    eve::dev::resetMcpActionGuide();

    constexpr const char* kPack = R"json({
      "schema":"eve.action.guide-pack","schemaVersion":1,"revision":3,
      "subject":{"domain":"animation.preview","id":1,"generation":1},
      "skeletonRef":"s","clipRef":"c","rootMode":"inPlace",
      "canvas":{"width":32,"height":32,"pixelsPerUnit":1,"backgroundRgba":[0,0,0,0],"transparent":true},
      "camera":{"projection":"orthographic","fovYDegrees":45,"orthoSize":1,"nearPlane":0.1,"farPlane":10,
                "eye":[0,0,5],"target":[0,0,0],"up":[0,1,0]},
      "shots":[{"shotId":"s0","timeSeconds":0,"view":"front","aovs":["silhouette","depth"],"tags":["windup"]}],
      "actionBinding":{"abilityId":"a","clipName":"c","fps":12,"loop":false,"phaseMarks":[]},
      "qc":{"minSilhouetteIoU":0.5,"maxJointErrorPx":4,"maxRetries":1}
    })json";

    Poco::JSON::Parser      parser;
    auto                    pack    = parser.parse(kPack).extract<Poco::JSON::Object::Ptr>();
    Poco::JSON::Object::Ptr setArgs = new Poco::JSON::Object();
    setArgs->set("pack", pack);
    REQUIRE(guideResult(eve::dev::callMcpActionGuideTool("eve_action_guide_pack_set", setArgs))->getValue<bool>("ok"));

    auto capture = guideResult(eve::dev::callMcpActionGuideTool("eve_action_guide_capture", nullptr));
    REQUIRE(capture->getValue<bool>("ok"));
    const auto token = capture->getValue<std::string>("token");

    Poco::JSON::Object::Ptr statusArgs = new Poco::JSON::Object();
    statusArgs->set("token", token);
    auto status = guideResult(eve::dev::callMcpActionGuideTool("eve_action_guide_status", statusArgs));
    REQUIRE(status->getValue<bool>("ok"));
    CHECK_EQ(status->getValue<std::string>("phase"), std::string("ready"));

    const auto root = std::filesystem::temp_directory_path() / "eve-action-guide-composition";
    std::filesystem::remove_all(root);
    Poco::JSON::Object::Ptr exportArgs = new Poco::JSON::Object();
    exportArgs->set("token", token);
    exportArgs->set("path", root.generic_string());
    auto exported = guideResult(eve::dev::callMcpActionGuideTool("eve_action_guide_export", exportArgs));
    REQUIRE(exported->getValue<bool>("ok"));
    CHECK(std::filesystem::exists(root / "manifest.json"));
    CHECK(std::filesystem::exists(root / "shots/0001/silhouette.png"));
    CHECK(std::filesystem::exists(root / "shots/0001/depth.png"));
    CHECK(std::filesystem::exists(root / "shots/0001/meta.json"));
    auto stem = eve::dev::actionGuideAovFileStem("guide.depth");
    REQUIRE(stem.ok());
    CHECK_EQ(stem.value(), std::string("depth"));
    std::filesystem::remove_all(root);
    eve::dev::resetMcpActionGuide();
}
