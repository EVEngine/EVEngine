#include "devtools/SceneInspect.hpp"

#include "common/Capability.h"
#include "common/RenderCapture.h"
#include "common/SceneQuery.h"

#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace eve::dev {
namespace {

std::string writeTextFile(const std::string& path, const std::string& text) {
    const std::filesystem::path p(path);
    std::error_code             ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary);
    if (!out.good()) return "cannot open output file: " + path;
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    out.flush();
    return out.good() ? std::string() : "failed writing: " + path;
}

}  // namespace

SceneInspect& SceneInspect::instance() {
    static SceneInspect inst;
    return inst;
}

eve::IRenderCapture* SceneInspect::captureIface() { return eve::cap::query<eve::IRenderCapture>(); }

eve::ISceneQuery* SceneInspect::scene() { return eve::cap::query<eve::ISceneQuery>(); }

bool SceneInspect::ensureCamera() {
    auto* cap = captureIface();
    return cap && cap->ensureCamera();
}

std::vector<InspectView> SceneInspect::generateViews(const glm::vec3& center, float fov) {
    std::vector<InspectView> out;
    const float              base = fov > 0.f ? fov : 60.f;

    {
        InspectView v;
        v.name    = "along_road";
        v.kind    = "沿路平视";
        v.eye     = center + glm::vec3(0.f, 1.8f, -6.f);
        v.target  = center + glm::vec3(0.f, 1.2f, 0.f);
        v.fovYDeg = base;
        out.push_back(v);
    }
    {
        InspectView v;
        v.name    = "bird_eye";
        v.kind    = "高空俯拍";
        v.eye     = center + glm::vec3(0.f, 30.f, 0.f);
        v.target  = center;
        v.fovYDeg = std::min(base * 1.2f, 100.f);
        out.push_back(v);
    }
    {
        InspectView v;
        v.name    = "corner_close";
        v.kind    = "拐角特写";
        v.eye     = center + glm::vec3(2.4f, 1.2f, 2.4f);
        v.target  = center + glm::vec3(0.f, 0.8f, 0.f);
        v.fovYDeg = std::max(base * 0.8f, 25.f);
        out.push_back(v);
    }
    {
        InspectView v;
        v.name    = "vista";
        v.kind    = "远景视角";
        v.eye     = center + glm::vec3(0.f, 14.f, -22.f);
        v.target  = center;
        v.fovYDeg = std::max(base * 0.85f, 35.f);
        out.push_back(v);
    }
    return out;
}

bool SceneInspect::setCameraPose(const glm::vec3& eye, const glm::vec3& rotYawPitch,
                                 float fov) {
    auto* cap = captureIface();
    if (!cap) return false;
    return cap->setCameraPoseYawPitch(eye.x, eye.y, eye.z, rotYawPitch.x, rotYawPitch.y, fov);
}

bool SceneInspect::setCameraView(const InspectView& v) {
    auto* cap = captureIface();
    if (!cap) return false;
    return cap->setCameraPose(v.eye.x, v.eye.y, v.eye.z, v.target.x, v.target.y, v.target.z, v.fovYDeg);
}

std::string SceneInspect::defaultCacheDir() const {
    std::string root = std::filesystem::current_path().string();
    for (char& c : root)
        if (c == '\\') c = '/';
    if (!root.empty() && root.back() == '/') root.pop_back();
    return root + "/eve_inspect_cache";
}

InspectCapture SceneInspect::capture(const std::string& outDir, const std::string& tag,
                                     const std::vector<std::string>& buffers) {
    InspectCapture result;
    auto*          cap = captureIface();
    if (!cap) {
        result.error = "Graphics module not available";
        return result;
    }

    bool wantColor = buffers.empty();
    bool wantDepth = false, wantNormal = false, wantId = false, wantShadow = false;
    for (const auto& b : buffers) {
        if (b == "color") wantColor = true;
        else if (b == "depth") wantDepth = true;
        else if (b == "normal") wantNormal = true;
        else if (b == "id") wantId = true;
        else if (b == "shadow") wantShadow = true;
    }
    if (!wantColor) wantColor = true;

    ensureCamera();
    cap->setReadbackEnabled(true);

    std::string dir = outDir;
    if (dir.empty()) {
        dir = cacheDir();
        if (dir.empty()) dir = defaultCacheDir();
    }
    for (char& c : dir)
        if (c == '\\') c = '/';

    const std::string safeTag       = tag.empty() ? "frame" : tag;
    const std::string pngPath       = dir + "/" + safeTag + ".png";
    const std::string jsonPath      = dir + "/" + safeTag + ".json";
    const std::string depthPngPath  = dir + "/" + safeTag + "_depth.png";
    const std::string normalPngPath = dir + "/" + safeTag + "_normal.png";
    const std::string idPngPath     = dir + "/" + safeTag + "_id.png";
    const std::string idJsonPath    = dir + "/" + safeTag + "_id.json";

    std::string err;
    if (!cap->savePng(pngPath, &result.width, &result.height, &err)) {
        result.error = err;
        return result;
    }

    const std::string geoJson = visibleEntitiesJson();
    if (const std::string werr = writeTextFile(jsonPath, geoJson); !werr.empty()) {
        result.error = werr;
        return result;
    }
    result.entityCount = 0;
    const auto pos     = geoJson.find("\"count\":");
    if (pos != std::string::npos) result.entityCount = std::atoi(geoJson.c_str() + pos + 9);

    if (wantDepth) {
        std::string berr;
        if (cap->gbufferPng("depth", depthPngPath, &berr))
            result.depthPngPath = depthPngPath;
        else
            result.unsupported.push_back("depth");
    }
    if (wantNormal) {
        std::string berr;
        if (cap->gbufferPng("normal", normalPngPath, &berr))
            result.normalPngPath = normalPngPath;
        else
            result.unsupported.push_back("normal");
    }
    if (wantShadow) result.unsupported.push_back("shadow");

    if (wantId) {
        int               iw = 0, ih = 0;
        bool              ok     = false;
        const std::string idJson = cap->entityIdMaskJson(&iw, &ih, &ok);
        if (!ok) {
            result.unsupported.push_back("id");
        } else {
            std::string perr;
            if (cap->entityIdMaskPng(idPngPath, &perr)) result.idPngPath = idPngPath;
            if (writeTextFile(idJsonPath, idJson).empty()) result.idJsonPath = idJsonPath;
        }
    }

    result.ok       = true;
    result.pngPath  = pngPath;
    result.jsonPath = jsonPath;
    lastPngPath_    = pngPath;
    lastJsonPath_   = jsonPath;
    return result;
}

std::string SceneInspect::currentPoseJson() {
    auto* cap = captureIface();
    return cap ? cap->cameraPoseJson() : "{\"error\":\"Graphics module not available\"}";
}

std::string SceneInspect::visibleEntitiesJson(const glm::vec3* eye, const glm::vec3* target,
                                              float fov) {
    auto* cap = captureIface();
    if (!cap) return "{\"error\":\"Graphics module not available\"}";
    bool ok = false;
    if (eye && target)
        return cap->visibleEntitiesJsonAt(eye->x, eye->y, eye->z, target->x, target->y, target->z, fov, &ok);
    return cap->visibleEntitiesJson(fov, &ok);
}

}  // namespace eve::dev
