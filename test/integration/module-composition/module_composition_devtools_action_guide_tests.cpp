// eve-test-policy: {"schema":"evengine.test-policy","version":1,"module":"devtools"}
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <algorithm>
#include <filesystem>
#include "common/ActionGuideIngest.h"
#include "common/ActionGuidePack.h"
#include "common/ActionGuideVerify.h"
#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "devtools/ActionGuideExport.hpp"
#include "devtools/ActionGuideIngestIO.hpp"
#include "devtools/ActionGuideVerifyIO.hpp"
#include "devtools/McpActionGuideTools.hpp"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <fstream>

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

TEST_CASE("module_composition.actionGuide.ingestWritesClipAndAbilityDraft") {
    using namespace eve;
    constexpr const char* kPack  = R"json({
      "schema":"eve.action.guide-pack","schemaVersion":1,"revision":9,
      "subject":{"domain":"animation.preview","id":1,"generation":1},
      "skeletonRef":"s","clipRef":"c","rootMode":"inPlace",
      "canvas":{"width":8,"height":8,"pixelsPerUnit":1,"backgroundRgba":[0,0,0,0],"transparent":true},
      "camera":{"projection":"orthographic","fovYDegrees":45,"orthoSize":1,"nearPlane":0.1,"farPlane":10,
                "eye":[0,0,5],"target":[0,0,0],"up":[0,1,0]},
      "shots":[{"shotId":"s0","timeSeconds":0,"view":"front","aovs":["silhouette"],"tags":[]}],
      "actionBinding":{"abilityId":"ability:hero.attack.light","clipName":"attack.light","fps":12,"loop":false,
                       "phaseMarks":[{"name":"active.start","timeSeconds":0.05},{"name":"recover.start","timeSeconds":0.1}]},
      "qc":{"minSilhouetteIoU":0.5,"maxJointErrorPx":4,"maxRetries":1}
    })json";
    auto                  parsed = Value::fromJson(kPack);
    REQUIRE(parsed.ok());
    auto pack = decodeActionGuidePack(parsed.value());
    REQUIRE(pack.ok());

    auto rgba = std::make_shared<std::vector<uint8_t>>(8 * 8 * 4, uint8_t{0});
    for (uint32_t y = 0; y < 8; ++y) {
        for (uint32_t x = 0; x < 8; ++x) {
            const size_t i    = (size_t(y) * 8 + x) * 4;
            const bool   edge = x == 0 || y == 0 || x == 7 || y == 7;
            (*rgba)[i + 0] = (*rgba)[i + 1] = (*rgba)[i + 2] = 255;
            (*rgba)[i + 3]                                   = edge ? 0 : 255;
        }
    }
    ActionGuideCandidateFrame frame;
    frame.meta   = {9, "s0", "unit-model", 1, "prompt-1"};
    frame.width  = 8;
    frame.height = 8;
    frame.rgba8  = rgba;

    ActionGuideIngestRequest request;
    request.pack   = pack.value();
    request.frames = {frame};
    auto draft     = buildActionGuideIngestDraft(request);
    REQUIRE(draft.ok());

    const auto root = std::filesystem::temp_directory_path() / "eve-action-guide-ingest";
    std::filesystem::remove_all(root);
    auto written = eve::dev::writeActionGuideIngestPack(draft.value(), request.frames, root);
    REQUIRE(written.ok());
    CHECK(std::filesystem::exists(root / "frames/frame_0.png"));
    CHECK(std::filesystem::exists(root / "clip.json"));
    CHECK(std::filesystem::exists(root / "ability.draft.json"));
    CHECK(std::filesystem::exists(root / "ingest.manifest.json"));

    // Round-trip PNG decode and MCP ingest path.
    const auto candidates = std::filesystem::temp_directory_path() / "eve-action-guide-candidates";
    std::filesystem::remove_all(candidates);
    std::filesystem::create_directories(candidates / "0001");
    std::filesystem::copy_file(root / "frames/frame_0.png", candidates / "0001/frame.png");
    auto metaJson = encodeActionGuideCandidateMeta(frame.meta);
    REQUIRE(metaJson.ok());
    auto metaText = metaJson.value().toJson();
    REQUIRE(metaText.ok());
    {
        std::ofstream out(candidates / "0001/meta.json", std::ios::binary | std::ios::trunc);
        out.write(metaText.value().data(), static_cast<std::streamsize>(metaText.value().size()));
    }

    eve::dev::resetMcpActionGuide();
    Poco::JSON::Parser      parser;
    auto                    packObj = parser.parse(kPack).extract<Poco::JSON::Object::Ptr>();
    Poco::JSON::Object::Ptr setArgs = new Poco::JSON::Object();
    setArgs->set("pack", packObj);
    REQUIRE(guideResult(eve::dev::callMcpActionGuideTool("eve_action_guide_pack_set", setArgs))->getValue<bool>("ok"));

    const auto outRoot = std::filesystem::temp_directory_path() / "eve-action-guide-ingest-mcp";
    std::filesystem::remove_all(outRoot);
    Poco::JSON::Object::Ptr ingestArgs = new Poco::JSON::Object();
    ingestArgs->set("candidateDir", candidates.generic_string());
    ingestArgs->set("outputDir", outRoot.generic_string());
    ingestArgs->set("expectedRevision", 9);
    auto ingested = guideResult(eve::dev::callMcpActionGuideTool("eve_action_guide_ingest", ingestArgs));
    REQUIRE(ingested->getValue<bool>("ok"));
    CHECK_EQ(ingested->getValue<std::string>("clipName"), std::string("attack.light"));
    CHECK(std::filesystem::exists(outRoot / "ability.draft.json"));
    CHECK(std::filesystem::exists(outRoot / "frames/frame_0.png"));

    std::filesystem::remove_all(root);
    std::filesystem::remove_all(candidates);
    std::filesystem::remove_all(outRoot);
    eve::dev::resetMcpActionGuide();
}

TEST_CASE("module_composition.actionGuide.verifyRejectsShiftedCandidate") {
    using namespace eve;
    constexpr const char* kPack  = R"json({
      "schema":"eve.action.guide-pack","schemaVersion":1,"revision":3,
      "subject":{"domain":"animation.preview","id":1,"generation":1},
      "skeletonRef":"s","clipRef":"c","rootMode":"inPlace",
      "canvas":{"width":16,"height":16,"pixelsPerUnit":1,"backgroundRgba":[0,0,0,0],"transparent":true},
      "camera":{"projection":"orthographic","fovYDegrees":45,"orthoSize":1,"nearPlane":0.1,"farPlane":10,
                "eye":[0,0,5],"target":[0,0,0],"up":[0,1,0]},
      "shots":[{"shotId":"s0","timeSeconds":0,"view":"front","aovs":["silhouette"],"tags":[]}],
      "actionBinding":{"abilityId":"ability:hero.attack.light","clipName":"attack.light","fps":12,"loop":false,
                       "phaseMarks":[]},
      "qc":{"minSilhouetteIoU":0.8,"maxJointErrorPx":4,"maxRetries":1}
    })json";
    auto                  parsed = Value::fromJson(kPack);
    REQUIRE(parsed.ok());
    auto pack = decodeActionGuidePack(parsed.value());
    REQUIRE(pack.ok());

    auto guideRgba = std::make_shared<std::vector<uint8_t>>(16 * 16 * 4, uint8_t{0});
    auto goodCand  = std::make_shared<std::vector<uint8_t>>(16 * 16 * 4, uint8_t{0});
    auto badCand   = std::make_shared<std::vector<uint8_t>>(16 * 16 * 4, uint8_t{0});
    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            const size_t i       = (size_t(y) * 16 + x) * 4;
            const int    dx      = int(x) - 8;
            const int    dy      = int(y) - 8;
            const bool   inside  = dx * dx + dy * dy <= 16;
            const int    dx2     = int(x) - 13;
            const bool   shifted = dx2 * dx2 + dy * dy <= 16;
            (*guideRgba)[i + 0] = (*guideRgba)[i + 1] = (*guideRgba)[i + 2] = inside ? 255 : 0;
            (*guideRgba)[i + 3]                                             = 255;
            (*goodCand)[i + 0] = (*goodCand)[i + 1] = (*goodCand)[i + 2] = 255;
            (*goodCand)[i + 3]                                           = inside ? 255 : 0;
            (*badCand)[i + 0] = (*badCand)[i + 1] = (*badCand)[i + 2] = 255;
            (*badCand)[i + 3]                                         = shifted ? 255 : 0;
        }
    }

    // Materialize guide artifact via Fake capture export.
    struct Fake final : IActionGuideCapture {
        ActionGuideCaptureSnapshot snap;
        Result<std::string>        requestCapture(std::string_view, std::span<const std::string>) override {
            return Result<std::string>::success(snap.token);
        }
        Result<ActionGuideCaptureSnapshot> captureSnapshot(std::string_view) override {
            return Result<ActionGuideCaptureSnapshot>::success(snap);
        }
        Result<void> releaseCapture(std::string_view) override { return Result<void>::success(); }
    } fake;
    struct Registration {
        IActionGuideCapture* previous = cap::query<IActionGuideCapture>();
        ~Registration() { cap::provide<IActionGuideCapture>(previous); }
    } registration;
    fake.snap.token    = "verify-guide";
    fake.snap.phase    = ActionGuideCapturePhase::Ready;
    fake.snap.revision = pack.value().revision;
    fake.snap.backend  = "composition-fake";
    fake.snap.pack     = pack.value();
    ActionGuideShotArtifact artifact;
    artifact.shotId = "s0";
    artifact.view   = "front";
    artifact.aovs.push_back(ActionGuideAovBuffer{"silhouette", 16, 16, guideRgba});
    fake.snap.shots.push_back(std::move(artifact));
    eve::cap::provide<IActionGuideCapture>(&fake);

    const auto guideRoot = std::filesystem::temp_directory_path() / "eve-action-guide-verify-guide";
    std::filesystem::remove_all(guideRoot);
    auto exported = eve::dev::writeActionGuideArtifactPack(fake.snap, guideRoot);
    REQUIRE(exported.ok());

    auto writeCandidateDir = [&](const std::filesystem::path& dir, const std::shared_ptr<std::vector<uint8_t>>& rgba) {
        std::filesystem::remove_all(dir);
        ActionGuideCandidateFrame frame;
        frame.meta   = {pack.value().revision, "s0", "unit", 1, "hash"};
        frame.width  = 16;
        frame.height = 16;
        frame.rgba8  = rgba;
        ActionGuideIngestRequest request;
        request.pack                   = pack.value();
        request.frames                 = {frame};
        request.requireTransparentEdge = false;  // shifted masks may touch the canvas edge
        auto draft                     = buildActionGuideIngestDraft(request);
        REQUIRE(draft.ok());
        const auto staging = dir / "_ingest";
        auto       written = eve::dev::writeActionGuideIngestPack(draft.value(), request.frames, staging);
        REQUIRE(written.ok());
        std::filesystem::create_directories(dir / "0001");
        std::filesystem::copy_file(staging / "frames/frame_0.png", dir / "0001/frame.png");
        std::filesystem::remove_all(staging);
    };

    const auto goodDir = std::filesystem::temp_directory_path() / "eve-action-guide-verify-good";
    const auto badDir  = std::filesystem::temp_directory_path() / "eve-action-guide-verify-bad";
    const auto outDir  = std::filesystem::temp_directory_path() / "eve-action-guide-verify-out";
    writeCandidateDir(goodDir, goodCand);
    writeCandidateDir(badDir, badCand);
    std::filesystem::remove_all(outDir);

    eve::dev::resetMcpActionGuide();
    Poco::JSON::Parser      parser;
    auto                    packObj = parser.parse(kPack).extract<Poco::JSON::Object::Ptr>();
    Poco::JSON::Object::Ptr setArgs = new Poco::JSON::Object();
    setArgs->set("pack", packObj);
    REQUIRE(guideResult(eve::dev::callMcpActionGuideTool("eve_action_guide_pack_set", setArgs))->getValue<bool>("ok"));

    Poco::JSON::Object::Ptr passArgs = new Poco::JSON::Object();
    passArgs->set("guideDir", guideRoot.generic_string());
    passArgs->set("candidateDir", goodDir.generic_string());
    passArgs->set("outputDir", outDir.generic_string());
    passArgs->set("expectedRevision", 3);
    auto passed = guideResult(eve::dev::callMcpActionGuideTool("eve_action_guide_verify", passArgs));
    REQUIRE(passed->getValue<bool>("ok"));
    CHECK(passed->getValue<bool>("passed"));
    CHECK(std::filesystem::exists(outDir / "verify.evidence.json"));

    Poco::JSON::Object::Ptr failArgs = new Poco::JSON::Object();
    failArgs->set("guideDir", guideRoot.generic_string());
    failArgs->set("candidateDir", badDir.generic_string());
    failArgs->set("expectedRevision", 3);
    auto rejected = guideResult(eve::dev::callMcpActionGuideTool("eve_action_guide_verify", failArgs));
    CHECK(!rejected->getValue<bool>("ok"));
    CHECK(!rejected->getValue<bool>("passed"));

    std::filesystem::remove_all(guideRoot);
    std::filesystem::remove_all(goodDir);
    std::filesystem::remove_all(badDir);
    std::filesystem::remove_all(outDir);
    eve::dev::resetMcpActionGuide();
}
