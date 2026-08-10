#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "animation/AnimImporter.h"
#include "animation/AnimSkeleton.h"
#include "animation/AnimClip.h"
#include "common/Exception.h"

#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: convert_mixamo <in.fbx> <out.eva> [clipName]\n";
        return 1;
    }
    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(
        argv[1], aiProcess_JoinIdenticalVertices | aiProcess_LimitBoneWeights);
    if (!scene) {
        std::cerr << "assimp failed: " << importer.GetErrorString() << "\n";
        return 2;
    }
    try {
        auto *sk = eve::animation::AnimImporter::loadSkeleton(scene);
        auto *clip = eve::animation::AnimImporter::loadClip(scene, sk, 0);
        if (argc > 3) clip->setName(argv[3]);
        clip->setSampleRate(15.f);
        const std::string text = eve::animation::AnimImporter::exportEva(sk, clip);
        std::ofstream out(argv[2]);
        out << text;
        std::cout << "wrote " << argv[2] << " bones=" << sk->getBoneCount()
                  << " duration=" << clip->getDuration()
                  << " bytes=" << text.size() << "\n";
        delete clip; delete sk;
    } catch (const eve::Exception &e) {
        std::cerr << e.what() << "\n";
        return 3;
    }
    return 0;
}
