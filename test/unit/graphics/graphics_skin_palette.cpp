#include <limits>
#include <vector>
#include "graphics/Mesh.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("graphics.skinPalette.dynamicOwnershipAndRollback") {
    eve::graphics::Mesh mesh;
    for (int count : {129, 204, 1024, 4096, 17}) {
        std::vector<float> matrices(size_t(count) * 16, 0.f);
        for (int i = 0; i < count; ++i) {
            matrices[size_t(i) * 16 + 15] = 1.f;
            matrices[size_t(i) * 16 + 12] = float(i);
        }
        REQUIRE(mesh.setSkinPalette(matrices.data(), count).ok());
        matrices.back() = 5.f;
        REQUIRE_EQ(mesh.getSkinPaletteCount(), count);
        REQUIRE_EQ(mesh.skinPalette().back(), 1.f);
    }
    const auto before = mesh.skinPalette();
    REQUIRE(!mesh.setSkinPalette(nullptr, 20).ok());
    REQUIRE(!mesh.setSkinPalette(before.data(), -1).ok());
    REQUIRE_EQ(mesh.skinPalette(), before);
    auto invalid = before;
    invalid[0]   = std::numeric_limits<float>::quiet_NaN();
    REQUIRE(!mesh.setSkinPalette(invalid.data(), 17).ok());
    REQUIRE_EQ(mesh.skinPalette(), before);
}
