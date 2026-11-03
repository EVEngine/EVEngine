#include "devtools/ActionGuideExport.hpp"

#include "common/Diagnostic.h"
#include "common/FrameCapture.h"
#include "devtools/FrameCapturePreview.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace eve::dev {
namespace {

template <class T>
Result<T> fail(DiagnosticCode code, std::string message, std::string path = {}) {
    return Result<T>::failure(Diagnostic::error(code, std::move(message), std::move(path)));
}

void writeBytes(const std::filesystem::path& path, const char* data, size_t size) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(data, static_cast<std::streamsize>(size));
    if (!stream) throw std::runtime_error("failed writing " + path.generic_string());
}

std::string padShotFolder(size_t index1Based) {
    std::ostringstream oss;
    oss << "shots/" << std::setw(4) << std::setfill('0') << index1Based;
    return oss.str();
}

Value canvasValue(const ActionGuideCanvas& canvas) {
    return Value::object(
        {{"width", Value(static_cast<std::int64_t>(canvas.width))},
         {"height", Value(static_cast<std::int64_t>(canvas.height))},
         {"pixelsPerUnit", Value(canvas.pixelsPerUnit)},
         {"backgroundRgba", Value::array({Value(canvas.backgroundRgba[0]), Value(canvas.backgroundRgba[1]),
                                          Value(canvas.backgroundRgba[2]), Value(canvas.backgroundRgba[3])})},
         {"transparent", Value(canvas.transparent)}});
}

Value cameraValue(const ActionGuideCamera& camera) {
    return Value::object(
        {{"projection",
          Value(camera.projection == ActionGuideProjection::Perspective ? "perspective" : "orthographic")},
         {"fovYDegrees", Value(camera.fovYDegrees)},
         {"orthoSize", Value(camera.orthoSize)},
         {"nearPlane", Value(camera.nearPlane)},
         {"farPlane", Value(camera.farPlane)},
         {"eye", Value::array({Value(camera.eye[0]), Value(camera.eye[1]), Value(camera.eye[2])})},
         {"target", Value::array({Value(camera.target[0]), Value(camera.target[1]), Value(camera.target[2])})},
         {"up", Value::array({Value(camera.up[0]), Value(camera.up[1]), Value(camera.up[2])})}});
}

Result<std::vector<uint8_t>> encodeRgbaPng(const ActionGuideAovBuffer& aov) {
    if (!aov.rgba8 || aov.width == 0 || aov.height == 0)
        return fail<std::vector<uint8_t>>(DiagnosticCode::InvalidArgument, "AOV buffer is empty", aov.aov);
    if (uint64_t(aov.width) * aov.height * 4 != aov.rgba8->size())
        return fail<std::vector<uint8_t>>(DiagnosticCode::InvalidArgument, "AOV RGBA size mismatch", aov.aov);
    FrameCaptureBuffer buffer;
    buffer.attachment = FrameAttachment::Final;
    buffer.format     = FramePixelFormat::Rgba8;
    buffer.width      = aov.width;
    buffer.height     = aov.height;
    buffer.bytes      = aov.rgba8;
    FramePreviewOptions options;
    options.view      = "rawChannels";
    options.maxWidth  = aov.width;
    options.maxHeight = aov.height;
    options.maxBytes  = size_t(aov.width) * aov.height * 4 + 64u * 1024u;
    auto preview      = frameCapturePreview(buffer, options);
    if (!preview) return Result<std::vector<uint8_t>>::failure(preview.status());
    return Result<std::vector<uint8_t>>::success(std::move(preview.value().png));
}

}  // namespace

Result<std::string> actionGuideAovFileStem(std::string_view aov) {
    if (aov == "silhouette" || aov == "guide.silhouette") return Result<std::string>::success("silhouette");
    if (aov == "depth" || aov == "guide.depth") return Result<std::string>::success("depth");
    if (aov == "bone" || aov == "guide.bone") return Result<std::string>::success("bone");
    if (aov == "partId" || aov == "guide.partId") return Result<std::string>::success("partId");
    if (aov == "normal" || aov == "guide.normal") return Result<std::string>::success("normal");
    if (aov == "final" || aov == "whitebox_final") return Result<std::string>::success("whitebox_final");
    return fail<std::string>(DiagnosticCode::InvalidArgument, "unsupported guide AOV name", std::string(aov));
}

Result<Value> writeActionGuideArtifactPack(const ActionGuideCaptureSnapshot& snapshot,
                                           const std::filesystem::path&      root) {
    if (snapshot.phase != ActionGuideCapturePhase::Ready)
        return fail<Value>(DiagnosticCode::InvalidArgument, "capture is not ready for export");
    if (snapshot.shots.empty())
        return fail<Value>(DiagnosticCode::InvalidArgument, "ready capture has no shot artifacts");
    try {
        std::filesystem::create_directories(root);
    } catch (const std::exception& e) {
        return fail<Value>(DiagnosticCode::Failed, std::string("create export root: ") + e.what());
    }

    Value::Array shotIndex;
    Value::Array files;

    try {
        for (size_t i = 0; i < snapshot.shots.size(); ++i) {
            const auto& shot    = snapshot.shots[i];
            const auto  folder  = padShotFolder(i + 1);
            const auto  shotDir = root / folder;
            std::filesystem::create_directories(shotDir);

            Value::Array  aovNames;
            Value::Object shotFiles;
            for (const auto& aov : shot.aovs) {
                auto stem = actionGuideAovFileStem(aov.aov);
                if (!stem) return Result<Value>::failure(stem.status());
                auto png = encodeRgbaPng(aov);
                if (!png) return Result<Value>::failure(png.status());
                const auto fileName = stem.value() + ".png";
                const auto relPath  = folder + "/" + fileName;
                const auto tmpPath  = shotDir / (fileName + ".tmp");
                const auto dstPath  = shotDir / fileName;
                writeBytes(tmpPath, reinterpret_cast<const char*>(png.value().data()), png.value().size());
                std::filesystem::rename(tmpPath, dstPath);
                aovNames.emplace_back(aov.aov);
                shotFiles.emplace(stem.value(), Value(relPath));
                files.emplace_back(relPath);
            }

            Value::Array tags;
            for (const auto& tag : shot.tags) tags.emplace_back(tag);

            Value::Object meta;
            meta.emplace("shotId", Value(shot.shotId));
            meta.emplace("timeSeconds", Value(shot.timeSeconds));
            if (shot.sampleIndex) meta.emplace("sampleIndex", Value(*shot.sampleIndex));
            meta.emplace("view", Value(shot.view));
            meta.emplace("aovs", Value(std::move(aovNames)));
            meta.emplace("tags", Value(std::move(tags)));
            meta.emplace("canvas", canvasValue(snapshot.pack.canvas));
            meta.emplace("camera", cameraValue(snapshot.pack.camera));
            meta.emplace("pixelsPerUnit", Value(snapshot.pack.canvas.pixelsPerUnit));
            meta.emplace("depthEncoding", Value("normalized-gray8-near-far"));
            meta.emplace("files", Value(std::move(shotFiles)));

            const auto metaJson = Value(std::move(meta)).toJson();
            if (!metaJson) return Result<Value>::failure(metaJson.status());
            const auto metaTmp = shotDir / "meta.json.tmp";
            const auto metaDst = shotDir / "meta.json";
            writeBytes(metaTmp, metaJson.value().data(), metaJson.value().size());
            std::filesystem::rename(metaTmp, metaDst);
            files.emplace_back(folder + "/meta.json");

            shotIndex.emplace_back(Value::object({{"shotId", Value(shot.shotId)}, {"folder", Value(folder)}}));
        }

        Value      manifest     = Value::object({{"schemaId", Value("eve.action.guide-pack.artifact")},
                                                 {"schemaVersion", Value(static_cast<std::int64_t>(1))},
                                                 {"revision", Value(static_cast<std::int64_t>(snapshot.revision))},
                                                 {"backend", Value(snapshot.backend)},
                                                 {"token", Value(snapshot.token)},
                                                 {"shotCount", Value(static_cast<std::int64_t>(snapshot.shots.size()))},
                                                 {"shots", Value(std::move(shotIndex))}});
        const auto manifestJson = manifest.toJson();
        if (!manifestJson) return Result<Value>::failure(manifestJson.status());
        const auto manifestTmp = root / "manifest.json.tmp";
        const auto manifestDst = root / "manifest.json";
        writeBytes(manifestTmp, manifestJson.value().data(), manifestJson.value().size());
        std::filesystem::rename(manifestTmp, manifestDst);

        return Result<Value>::success(
            Value::object({{"exportRoot", Value(root.generic_string())},
                           {"manifest", Value("manifest.json")},
                           {"files", Value(std::move(files))},
                           {"shotCount", Value(static_cast<std::int64_t>(snapshot.shots.size()))},
                           {"revision", Value(static_cast<std::int64_t>(snapshot.revision))},
                           {"backend", Value(snapshot.backend)}}));
    } catch (const std::exception& e) {
        return fail<Value>(DiagnosticCode::Failed, std::string("guide export failed: ") + e.what());
    }
}

}  // namespace eve::dev
