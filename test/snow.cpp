#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cmath>

#include "procgen/heightmap/Heightmap.h"
#include "snow/Snow.h"
#include "snow/SnowField.h"

using eve::snow::SnowField;

TEST_CASE("SnowField.basicGrid") {
    SnowField f(8, 6);
    CHECK_EQ(f.getWidth(), 8);
    CHECK_EQ(f.getHeight(), 6);
    CHECK_EQ(f.height(3, 2), 0.f);
    CHECK_EQ(f.height(-1, 0), 0.f);  // out-of-bounds reads 0

    f.setHeight(3, 2, 0.8f);
    CHECK_EQ(f.height(3, 2), 0.8f);
    f.fill(0.5f);
    CHECK_EQ(f.height(0, 0), 0.5f);

    f.setHeight(0, 0, 2.f);  // clamped to [0,1]
    CHECK_EQ(f.height(0, 0), 1.f);
    f.setHeight(0, 0, -0.5f);
    CHECK_EQ(f.height(0, 0), 0.f);
    f.setHeight(99, 99, 0.7f);  // out-of-bounds write is a no-op

    f.resize(4, 4);
    CHECK_EQ(f.getWidth(), 4);
    CHECK_EQ(f.getHeight(), 4);
    CHECK(f.isDirty());
}

TEST_CASE("SnowField.footprintDepressesAndRaisesRim") {
    SnowField f(16, 16);
    f.fill(0.8f);
    f.clearDirty();

    f.stampFootprint(8.f, 8.f, 1.f, 0.f, 3.f, 0.5f);
    CHECK(f.isDirty());

    CHECK(f.height(8, 8) < 0.4f);          // center pressed down
    CHECK(f.height(10, 8) < 0.75f);        // inner footprint lowered
    CHECK(f.height(8, 10) == 0.8f);        // narrower across: untouched
    CHECK(f.height(11, 8) > 0.85f);        // pushed-up rim at the edge
    CHECK(f.height(2, 2) == 0.8f);         // far away untouched

    f.clearDirty();
    CHECK(!f.isDirty());
}

TEST_CASE("SnowField.footprintFollowsTravelDirection") {
    SnowField f(16, 16);
    f.fill(0.8f);
    f.stampFootprint(8.f, 8.f, 0.f, 1.f, 3.f, 0.5f);

    CHECK(f.height(8, 10) < 0.75f);        // elongated along +Z
    CHECK(f.height(10, 8) == 0.8f);        // across direction untouched
}

TEST_CASE("SnowField.impactCraterBowlAndRim") {
    SnowField f(20, 20);
    f.fill(0.8f);
    f.stampImpact(10.f, 10.f, 4.f, 0.6f);

    CHECK(f.height(10, 10) < 0.21f);       // deep bowl center
    CHECK(f.height(13, 10) < 0.78f);       // shallow bowl near the edge
    CHECK(f.height(14, 10) > 0.85f);       // raised ejecta ring outside the bowl
    CHECK(f.height(16, 10) == 0.8f);       // outside the rim band

    // Ejecta is intentionally lumpy: ring heights vary per cell instead of
    // forming a perfect smooth ring.
    float lo = 1.f, hi = 0.f;
    for (int z = 5; z <= 15; ++z) {
        for (int x = 5; x <= 15; ++x) {
            const float dx = float(x) - 10.f;
            const float dz = float(z) - 10.f;
            const float n = std::sqrt(dx * dx + dz * dz) / 4.f;
            if (n < 0.95f || n > 1.4f) continue;
            lo = std::min(lo, f.height(x, z));
            hi = std::max(hi, f.height(x, z));
        }
    }
    CHECK(hi - lo > 0.05f);
}

TEST_CASE("SnowField.snowfallRecovery") {
    SnowField f(4, 4);
    f.fill(0.5f);
    f.clearDirty();

    f.addSnowfall(0.2f);
    CHECK(f.isDirty());
    CHECK(std::fabs(f.height(0, 0) - 0.7f) < 1e-5f);

    f.addSnowfall(0.5f);
    CHECK_EQ(f.height(0, 0), 1.f);  // clamped at full cover
    f.addSnowfall(-1.f);            // negative amounts are ignored
    CHECK_EQ(f.height(0, 0), 1.f);
}

TEST_CASE("SnowField.heightTexture") {
    SnowField f(3, 2);
    f.fill(1.f);
    std::vector<uint8_t> rgba = f.toHeightRGBA();
    CHECK_EQ(int(rgba.size()), 3 * 2 * 4);
    CHECK_EQ(int(rgba[0]), 255);   // R = height
    CHECK_EQ(int(rgba[12]), 255);  // R of the last cell
    CHECK_EQ(int(rgba[1]), 0);     // G/B stay 0 so albedo stays clean
    CHECK_EQ(int(rgba[2]), 0);

    f.setHeight(0, 0, 0.5f);
    rgba = f.toHeightRGBA();
    CHECK(int(rgba[0]) >= 126);
    CHECK(int(rgba[0]) <= 130);
    CHECK_EQ(int(rgba[3]), 255);   // alpha always opaque
}

TEST_CASE("SnowField.albedoBlendsSnowToGround") {
    SnowField f(4, 4);
    f.fill(0.f);
    std::vector<uint8_t> rgba = f.toAlbedoRGBA();
    CHECK(int(rgba[0]) >= 85);   // dark soil
    CHECK(int(rgba[0]) <= 100);
    CHECK(int(rgba[1]) >= 70);
    CHECK(int(rgba[1]) <= 85);
    CHECK(int(rgba[2]) >= 55);
    CHECK(int(rgba[2]) <= 68);

    f.fill(1.f);
    rgba = f.toAlbedoRGBA();
    CHECK(int(rgba[0]) >= 230);  // cool white snow
    CHECK(int(rgba[0]) <= 245);
    CHECK(int(rgba[3]) == 255);

    // Albedo is a color ramp, not a copy of the height channel: at s = 0.25 the
    // red channel (~101, mostly ground) must differ from the height texture's
    // red (64).
    f.fill(0.25f);
    const int albedoR = int(f.toAlbedoRGBA()[0]);
    const int heightR = int(f.toHeightRGBA()[0]);
    CHECK(std::abs(albedoR - heightR) > 20);
}

TEST_CASE("SnowField.normalMapFromGradient") {
    SnowField f(16, 16);
    f.fill(0.5f);
    std::vector<uint8_t> flat = f.toNormalRGBA();
    CHECK(int(flat[0]) >= 126);  // flat = 128,128,255
    CHECK(int(flat[0]) <= 130);
    CHECK(int(flat[1]) >= 126);
    CHECK(int(flat[1]) <= 130);
    CHECK(int(flat[2]) >= 252);

    // Linearly rising snow toward +X tilts the normal away from +U.
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            f.setHeight(x, z, 0.4f + 0.05f * float(x));
        }
    }
    std::vector<uint8_t> slope = f.toNormalRGBA();
    const size_t i = (8 * 16 + 8) * 4;
    CHECK(int(slope[i + 0]) < 120);  // nx < 0 -> R below mid gray
    CHECK(int(slope[i + 1]) >= 126);
    CHECK(int(slope[i + 1]) <= 130);
    CHECK(int(slope[i + 2]) >= 245);
}

TEST_CASE("SnowField.applyToHeightmapAddsSnow") {
    eve::procgen::Heightmap terrain(8, 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            terrain.setHeight(x, y, 0.5f);
        }
    }

    SnowField f(8, 8);
    f.fill(0.5f);
    eve::procgen::Heightmap out;
    eve::snow::applySnowToHeightmap(f, terrain, out, 0.2f);
    CHECK_EQ(out.getWidth(), 8);
    CHECK(std::fabs(out.height(3, 3) - 0.6f) < 1e-5f);  // 0.5 + 0.5 * 0.2

    f.setHeight(2, 2, 0.f);
    eve::snow::applySnowToHeightmap(f, terrain, out, 0.2f);
    CHECK(std::fabs(out.height(2, 2) - 0.5f) < 1e-5f);  // bare ground

    SnowField small(4, 4);
    small.fill(1.f);
    eve::snow::applySnowToHeightmap(small, terrain, out, 0.2f);
    CHECK(std::fabs(out.height(6, 6) - 0.5f) < 1e-5f);  // outside field: terrain only
}
