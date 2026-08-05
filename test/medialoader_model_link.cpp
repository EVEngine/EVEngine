#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
#include "medialoader/model/ModelLoader.h"
#include <assimp/scene.h>

TEST_CASE("MedialoaderModel.linkHeaders") {
    medialoader::ModelLoader loader;
    CHECK(sizeof(aiScene) > 0);
    CHECK(sizeof(loader) > 0);
}
