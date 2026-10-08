#include "animation/AnimLattice.h"

#include "common/Exception.h"
#include "model3d/ModelData.h"

#include <assimp/mesh.h>
#include <assimp/scene.h>

namespace eve::animation {

AnimLattice* AnimLattice::fromModel(const model3d::ModelData* model, int meshIndex, int divX, int divY, int divZ) {
    if (!model) {
        throw Exception("AnimLattice.fromModel: null model");
    }
    const aiMesh* mesh = model->getMesh(meshIndex);
    if (!mesh) {
        throw Exception("AnimLattice.fromModel: invalid mesh index %d", meshIndex);
    }
    if (mesh->mNumVertices == 0 || !mesh->mVertices) {
        throw Exception("AnimLattice.fromModel: mesh %d has no vertices", meshIndex);
    }
    auto* lattice = new AnimLattice(divX, divY, divZ);
    lattice->bindPositions(reinterpret_cast<const float*>(mesh->mVertices), static_cast<int>(mesh->mNumVertices));
    return lattice;
}

void AnimLattice::bindModel(const model3d::ModelData* model, int meshIndex) {
    if (!model) {
        throw Exception("AnimLattice.bindModel: null model");
    }
    const aiMesh* mesh = model->getMesh(meshIndex);
    if (!mesh) {
        throw Exception("AnimLattice.bindModel: invalid mesh index %d", meshIndex);
    }
    if (mesh->mNumVertices == 0 || !mesh->mVertices) {
        throw Exception("AnimLattice.bindModel: mesh %d has no vertices", meshIndex);
    }
    bindPositions(reinterpret_cast<const float*>(mesh->mVertices), static_cast<int>(mesh->mNumVertices));
}

}  // namespace eve::animation
