#include "spritestack/SpriteStack.h"

#include "common/Exception.h"
#include "image/ImageData.h"
#include "model3d/ModelData.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <assimp/color4.h>
#include <assimp/mesh.h>
#include <assimp/vector3.h>

#include <cstdint>
#include <string>
#include <vector>

namespace eve::spritestack {

std::vector<image::ImageData *> sliceModelToLayers(model3d::ModelData *model, const SliceOptions &opt) {
    if (!model) throw eve::Exception("SpriteStack.sliceModel: null model");
    std::vector<float> pos, nrm, rgb;
    std::vector<uint32_t> idx;
    const int meshCount = model->getMeshCount();
    for (int m = 0; m < meshCount; ++m) {
        const aiMesh *am = model->getMesh(m);
        if (!am) continue;
        const uint32_t base = uint32_t(pos.size() / 3);
        for (unsigned v = 0; v < am->mNumVertices; ++v) {
            const aiVector3D &p = am->mVertices[v];
            pos.insert(pos.end(), {p.x, p.y, p.z});
            if (am->mNormals) {
                const aiVector3D &n = am->mNormals[v];
                nrm.insert(nrm.end(), {n.x, n.y, n.z});
            }
            if (am->mColors[0] != nullptr) {
                const aiColor4D &c = am->mColors[0][v];
                rgb.insert(rgb.end(), {c.r, c.g, c.b});
            }
        }
        for (unsigned f = 0; f < am->mNumFaces; ++f) {
            const auto &face = am->mFaces[f];
            if (face.mNumIndices != 3) continue;
            idx.insert(idx.end(),
                       {base + face.mIndices[0], base + face.mIndices[1], base + face.mIndices[2]});
        }
    }
    SliceInput in{};
    in.posXYZ = pos.data();
    in.nrmXYZ = nrm.empty() ? nullptr : nrm.data();
    in.rgb = rgb.empty() ? nullptr : rgb.data();
    in.vertexCount = int(pos.size() / 3);
    in.indices = idx.data();
    in.indexCount = int(idx.size());
    return sliceMeshToLayers(in, opt);
}

std::vector<image::ImageData *> SpriteStack::sliceModel(model3d::ModelData *model, int layerCount,
                                                        int imageW, int imageH, const std::string &axis,
                                                        float thickness) {
    SliceOptions options;
    options.layerCount = layerCount;
    options.imageW = imageW;
    options.imageH = imageH;
    options.axis = axis;
    options.thickness = thickness;
    return sliceModelToLayers(model, options);
}

void exposeSpriteStackModelBindings(ssq::Class &cls) {
    cls.addFunc("sliceModel", &SpriteStack::sliceModel);
}

}  // namespace eve::spritestack
