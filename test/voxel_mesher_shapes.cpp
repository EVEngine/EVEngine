#include "voxel/Chunk.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cstdio>
#include <memory>

using namespace eve::voxel;

TEST_CASE("voxel.greedy.solid_cube") {
    for (const uint8_t texture : {uint8_t(1), uint8_t(7)}) {
        std::printf("solid cube texture=%u\n", unsigned(texture));
        std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
        chunk->fill(texture);
        chunk->remesh();
        REQUIRE_EQ(chunk->totalRectCount(), 6);
        for (int i = 0; i < faceDirCount(); ++i) {
            std::printf("face=%d\n", i);
            const FaceDir direction = FaceDir(i);
            REQUIRE_EQ(chunk->faceRectCount(direction), 1);
            const PackedRect &rect = chunk->faceRects(direction)[0];
            REQUIRE_EQ(rect.width(), 32);
            REQUIRE_EQ(rect.height(), 32);
            REQUIRE_EQ(rect.tex(), texture);
        }
        REQUIRE_EQ(chunk->faceRects(FaceDir::PosY)[0].y(), 31);
    }
}

TEST_CASE("voxel.greedy.stripesMergeAlongBothAxes") {
    struct Stripe {
        int     x, z;
        int     stepX, stepZ;
        int     length;
        uint8_t texture;
        int     width, height;
    };
    for (const Stripe stripe : {Stripe{0, 3, 1, 0, 16, 4, 16, 1}, Stripe{5, 0, 0, 1, 12, 2, 1, 12}}) {
        std::printf("stripe step=(%d,%d) length=%d\n", stripe.stepX, stripe.stepZ, stripe.length);
        std::unique_ptr<Chunk> chunk(new Chunk(0, 0, 0));
        for (int i = 0; i < stripe.length; ++i)
            chunk->set(stripe.x + i * stripe.stepX, 0, stripe.z + i * stripe.stepZ, stripe.texture);
        chunk->remesh();
        REQUIRE_EQ(chunk->faceRectCount(FaceDir::PosY), 1);
        const PackedRect &top = chunk->faceRects(FaceDir::PosY)[0];
        REQUIRE_EQ(top.width(), stripe.width);
        REQUIRE_EQ(top.height(), stripe.height);
        REQUIRE_EQ(top.x(), stripe.x);
        REQUIRE_EQ(top.y(), 0);
        REQUIRE_EQ(top.z(), stripe.z);
        REQUIRE_EQ(top.tex(), stripe.texture);
    }
}
