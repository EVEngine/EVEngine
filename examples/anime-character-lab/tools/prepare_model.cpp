#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <algorithm>
#include <assimp/Exporter.hpp>
#include <assimp/Importer.hpp>
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    Assimp::Importer importer;
    auto*            s = importer.ReadFile(argv[1], aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                                        aiProcess_PreTransformVertices | aiProcess_GenSmoothNormals);
    if (!s) {
        std::cerr << importer.GetErrorString();
        return 1;
    }
    unsigned   verts = 0, faces = 0;
    aiVector3D lo(1e9f), hi(-1e9f);
    for (unsigned m = 0; m < s->mNumMeshes; ++m) {
        auto* mesh = s->mMeshes[m];
        verts += mesh->mNumVertices;
        faces += mesh->mNumFaces;
        for (unsigned i = 0; i < mesh->mNumVertices; ++i) {
            auto p = mesh->mVertices[i];
            for (int c = 0; c < 3; ++c) {
                lo[c] = std::min(lo[c], p[c]);
                hi[c] = std::max(hi[c], p[c]);
            }
        }
        std::cout << m << " mat=" << mesh->mMaterialIndex << " vertices=" << mesh->mNumVertices
                  << " faces=" << mesh->mNumFaces << "\n";
    }
    std::cout << "total=" << verts << " faces=" << faces << " bounds=" << lo.x << "," << lo.y << "," << lo.z << " / "
              << hi.x << "," << hi.y << "," << hi.z << "\n";
    Assimp::Exporter exporter;
    if (exporter.Export(s, "obj", argv[2]) != AI_SUCCESS) {
        std::cerr << exporter.GetErrorString();
        return 1;
    }
}
