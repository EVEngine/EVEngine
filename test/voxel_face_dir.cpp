#include <string>
#include "voxel/FaceDir.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::voxel;

TEST_CASE("voxel.faceDir.name_aliases") {
    struct Direction {
        FaceDir     direction;
        const char* canonical;
        const char* alias;
    };
    const Direction directions[] = {{FaceDir::PosX, "posX", "+x"}, {FaceDir::NegX, "negX", "-x"},
                                    {FaceDir::PosY, "posY", "+y"}, {FaceDir::NegY, "negY", "-y"},
                                    {FaceDir::PosZ, "posZ", "+z"}, {FaceDir::NegZ, "negZ", "-z"}};
    for (const auto& expected : directions) {
        for (const char* name : {expected.canonical, expected.alias}) {
            FaceDir    actual{};
            const bool parsed = faceDirFromName(name, actual);
            REQUIRE(parsed);
            REQUIRE(actual == expected.direction);
        }
        REQUIRE(std::string(faceDirName(expected.direction)) == expected.canonical);
    }
    for (const char* invalid : {"forward", "", "+", "pos", "+w", "PosX", "x+"}) {
        FaceDir    actual{};
        const bool parsed = faceDirFromName(invalid, actual);
        REQUIRE(!parsed);
    }
}
