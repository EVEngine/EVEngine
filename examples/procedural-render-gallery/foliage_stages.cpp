#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "filesystem/FileData.h"
#include "graphics/Canvas.h"
#include "graphics/Graphics.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"
#include "graphics/Texture.h"
#include "image/Image.h"
#include "image/ImageData.h"
#include "procgen/MeshBuild.h"
#include "procgen/Params.h"
#include "procgen/Procgen.h"
#include "procgen/algorithms/FoliageCluster.h"
#include "procgen/algorithms/MarchingCubes.h"
#include "window/Window.h"

namespace {
using namespace eve::procgen;
using namespace eve::graphics;
void checked(bool success) {
    if (!success) throw std::runtime_error("Foliage stage render failed");
}
bool saveImagePng(const eve::image::ImageData &image, const std::string &path) {
    [[maybe_unused]] auto                     *module = eve::image::Image::create();
    std::unique_ptr<eve::filesystem::FileData> png(
        image.encode(medialoader::FormatHandler::ENCODED_PNG, path.c_str(), false));
    if (!png) return false;
    std::ofstream out(path, std::ios::binary);
    out.write(static_cast<const char *>(png->getData()), static_cast<std::streamsize>(png->getSize()));
    return out.good();
}
struct V3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

[[maybe_unused]] void addDot(MeshBuild &out, V3 c, float r) {
    const V3       axis[6] = {{r, 0, 0}, {-r, 0, 0}, {0, r, 0}, {0, -r, 0}, {0, 0, r}, {0, 0, -r}};
    const uint32_t base    = uint32_t(out.getVertexCount());
    for (int i = 0; i < 6; ++i) {
        const float length = std::sqrt(axis[i].x * axis[i].x + axis[i].y * axis[i].y + axis[i].z * axis[i].z);
        out.addVertex(c.x + axis[i].x, c.y + axis[i].y, c.z + axis[i].z, axis[i].x / length, axis[i].y / length,
                      axis[i].z / length, 0.75f, 0.5f);
    }
    const int faces[8][3] = {{0, 2, 4}, {2, 1, 4}, {1, 3, 4}, {3, 0, 4}, {2, 0, 5}, {1, 2, 5}, {3, 1, 5}, {0, 3, 5}};
    for (const auto &face : faces) {
        out.addTriangle(base + uint32_t(face[0]), base + uint32_t(face[1]), base + uint32_t(face[2]));
    }
}

/**
 * @brief Recover the sampled blue-noise centres from an emitted single-plane cluster.
 *
 * Every petal writes six consecutive vertices, so the mean of each group is the
 * sampled centre offset by a constant fraction of the leaf size along the
 * leaf's own long axis. That offset is small next to the blue-noise spacing and
 * irrelevant for a marker dot.
 */
[[maybe_unused]] std::vector<V3> recoverCentres(const MeshBuild &mesh) {
    std::vector<V3> centres;
    for (int i = 0; i + 5 < mesh.getVertexCount(); i += 6) {
        float x = 0.f, y = 0.f, z = 0.f;
        for (int v = 0; v < 6; ++v) {
            x += mesh.getPositionX(i + v);
            y += mesh.getPositionY(i + v);
            z += mesh.getPositionZ(i + v);
        }
        centres.push_back({x / 6.f, y / 6.f, z / 6.f});
    }
    return centres;
}

/** @brief One shared window/graphics pair for the whole stage walk. */
struct Stage {
    eve::window::Window *win    = nullptr;
    Graphics            *gfx    = nullptr;
    Texture             *atlas  = nullptr;
    Renderable3D        *object = nullptr;
    Camera3D            *camera = nullptr;
    std::string          directory;
};

[[maybe_unused]] bool openStage(Stage &stage, const std::string &directory) {
    stage.directory = directory;
    stage.win       = eve::window::Window::create();
    stage.gfx       = Graphics::create();
    if (!stage.win || !stage.gfx) return false;
    eve::window::WindowSettings settings;
    settings.width    = 900;
    settings.height   = 700;
    settings.centered = true;
    if (!stage.win->setWindowSettings(settings)) return false;

    // Left half of the atlas is bark, right half is foliage.
    const uint8_t atlasPixels[] = {
        111, 70, 42, 255, 128, 82, 47, 255, 64, 119, 57, 255, 82, 145, 67, 255,
    };
    stage.atlas = stage.gfx->newTexture(4, 1, atlasPixels);
    if (!stage.atlas) return false;

    stage.object = Renderable3D::create();
    stage.object->setTexture(stage.atlas);
    stage.object->setTint(1.f, 1.f, 1.f, 1.f);
    stage.object->setRoughness(0.88f);

    stage.camera = Camera3D::createCamera();
    stage.camera->setUp(0.f, 1.f, 0.f);
    stage.camera->setAmbient(0.30f, 0.34f, 0.28f);
    stage.camera->setActive(true);

    stage.gfx->setScreenReadbackEnabled(true);
    stage.gfx->setBackgroundColor(Color(0.075f, 0.105f, 0.095f, 1.f));
    RenderSystem3D::setDirectionalLight(-0.55f, -1.f, -0.35f, 1.45f, 1.32f, 1.08f);
    return true;
}

/** @brief Upload a CPU mesh, aim the camera, and write one stable frame. */
[[maybe_unused]] bool shoot(Stage &stage, const MeshBuild &mesh, float eyeX, float eyeY, float eyeZ, float targetX,
                            float targetY, float targetZ, float fov, const std::string &file) {
    Procgen generator;
    auto    uploaded = generator.uploadMeshBorrowed(mesh, *stage.gfx);
    if (!uploaded.isBound()) return false;
    stage.object->setMesh(uploaded.get());
    stage.camera->setEye(eyeX, eyeY, eyeZ);
    stage.camera->setTarget(targetX, targetY, targetZ);
    stage.camera->setFov(fov);
    for (int frame = 0; frame < 4; ++frame) {
        RenderSystem3D::render(*stage.gfx);
        RenderSystem::render(*stage.gfx);
    }
    std::unique_ptr<eve::image::ImageData> image(stage.gfx->newImageData());
    if (!image) return false;
    if (!saveImagePng(*image, stage.directory + "/" + file)) return false;
    std::printf("step render saved: %s\n", (stage.directory + "/" + file).c_str());
    return true;
}

[[maybe_unused]] FoliageClusterDesc stagePlaneDesc() {
    FoliageClusterDesc desc;
    desc.seed               = 20260910;
    desc.radius             = 0.85f;
    desc.leafSize           = 0.24f;
    desc.leafSpacing        = 0.95f;
    desc.planes             = 1;
    desc.capPlanes          = 0;
    desc.leafJitter         = 0.f;
    desc.leafScaleVariation = 0.22f;
    desc.maxLeavesPerPlane  = 256;
    return desc;
}

void renderStages(const char *directory) {
    Stage stage;
    checked(openStage(stage, directory));

    // ---- step 1: the blue-noise points on the plane -------------------------
    const FoliageClusterDesc desc = stagePlaneDesc();
    MeshBuild                plane;
    const int                leaves = addFoliageCluster(plane, desc);
    checked(leaves > 0);
    const std::vector<V3> centres = recoverCentres(plane);

    MeshBuild dots;
    for (const V3 &c : centres) addDot(dots, c, desc.leafSize * 0.16f);
    checked(shoot(stage, dots, 0.f, 0.f, 3.4f, 0.f, 0.f, 0.f, 40.f, "step1_blue_noise_points.png"));

    // ---- step 2: random leaves drawn at those points ------------------------
    checked(shoot(stage, plane, 0.f, 0.f, 3.4f, 0.f, 0.f, 0.f, 40.f, "step2_leaves_on_plane.png"));

    // ---- step 3: only the leaves carry geometry ----------------------------
    // Seen from an angle the bundle is a flat card full of holes: the area
    // between leaves is not drawn at all, which is how "leaves opaque, rest
    // transparent" is expressed by a CPU mesh.
    checked(shoot(stage, plane, 2.9f, 0.35f, 1.9f, 0.f, 0.f, 0.f, 40.f, "step3_only_leaves_flat.png"));

    // ---- step 4: 1 / 4 / 10 planes fanned around the cluster's Y axis -------
    // Seen from high above, a vertical leaf plane is an edge-on line: one plane
    // is a single line, ten planes form the star that gives the bundle volume.
    MeshBuild fan;
    for (int variant = 0; variant < 3; ++variant) {
        const int          counts[3] = {1, 4, 10};
        FoliageClusterDesc one       = desc;
        one.planes                   = counts[variant];
        one.capPlanes                = counts[variant] > 1 ? 2 : 0;
        one.seed                     = desc.seed + std::uint32_t(variant);
        MeshBuild part;
        addFoliageCluster(part, one);
        checked(fan.appendTransformed(&part, float(variant) * 2.5f - 2.5f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f));
    }
    checked(shoot(stage, fan, 0.f, 5.4f, 4.6f, 0.f, 0.f, 0.f, 40.f, "step4_planes_around_y.png"));

    // ---- step 5: flat card normals vs. the transferred sphere normal --------
    MeshBuild          normals;
    FoliageClusterDesc flat = desc;
    flat.planes             = 10;
    flat.capPlanes          = 2;
    flat.normalRounding     = 0.f;
    MeshBuild faceted;
    addFoliageCluster(faceted, flat);
    checked(normals.appendTransformed(&faceted, -1.35f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f));
    FoliageClusterDesc round = flat;
    round.normalRounding     = 1.f;
    round.seed               = desc.seed + 991u;
    MeshBuild rounded;
    addFoliageCluster(rounded, round);
    checked(normals.appendTransformed(&rounded, 1.35f, 0.f, 0.f, 0.f, 1.f, 1.f, 1.f));
    checked(shoot(stage, normals, 0.f, 0.5f, 4.4f, 0.f, 0.f, 0.f, 40.f, "step5_spherical_normals.png"));

    // ---- step 6: random branches + clusters = a whole tree ------------------
    Params params;
    params.setSeed(31415);
    params.setString("style", "lowpoly");
    params.setString("leafMode", "clusters");
    params.setString("branchAlgorithm", "spaceColonization");
    params.setFloat("leafDensity", 0.82f);
    params.setFloat("height", 6.2f);
    params.setFloat("crownRadius", 2.15f);
    params.setInt("attractorCount", 120);
    params.setInt("colonizationIterations", 38);
    params.setFloat("growthStep", 0.25f);
    MeshBuild   tree;
    std::string error;
    checked(MeshRecipeRegistry::instance().generate("mesh.tree", params, tree, error));
    stage.object->setRotation(-18.f, 0.f, 0.f);
    checked(shoot(stage, tree, 9.2f, 5.1f, 11.4f, 0.f, 3.1f, 0.f, 36.f, "step6_tree.png"));

    stage.win->close();
}

}  // namespace
int main() {
    try {
        std::filesystem::create_directories("stages");
        renderStages("stages");
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
