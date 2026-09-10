#include <cmath>
#include <filesystem>
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/RenderSystem3D.h"
#include "medialoader/model/ModelLoader.h"
#include "model3d/ModelData.h"
#include "model3d/ModelRenderer.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
TEST_CASE("model3d.gltfUvOriginAndTransformSurviveLoader") {
    auto* gfx = eve::graphics::Graphics::create();
    gfx->initHeadless(128, 128);
    const auto               source = std::filesystem::path(__FILE__).parent_path() / "fixtures/model3d/uv-origin.gltf";
    medialoader::ModelLoader loader;
    medialoader::LoadOptions options;
    options.joinIdenticalVertices = false;
    options.improveCacheLocality  = false;
    for (bool flip : {false, true}) {
        options.flipUVs               = flip;
        auto                    scene = loader.loadFromPath(source.string().c_str(), options);
        eve::model3d::ModelData model(std::move(scene), "", flip);
        auto*                   entity = eve::model3d::buildRenderable(*gfx, &model, 0);
        REQUIRE(entity != nullptr);
        const auto uv = entity->getMesh()->texcoordSet(0);
        REQUIRE_EQ(uv.size(), size_t(6));
        REQUIRE(std::abs(uv[1] - .2f) < .00001f);
        const auto binding = entity->getMaterial()->pbrSurface().textures[0];
        REQUIRE(std::abs(binding.rotation - .6f) < .00001f);
        REQUIRE(std::abs(binding.offset[0] - .13f) < .00001f);
        REQUIRE(std::abs(binding.offset[1] - .27f) < .00001f);
    }
}
