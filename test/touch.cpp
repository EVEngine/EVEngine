#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "common/Exception.h"
#include "touch/Touch.h"

#include <cstdint>

TEST_CASE("touch.getTouchesEmpty") {
    auto* t = eve::touch::Touch::create();
    REQUIRE(t != nullptr);
    CHECK(t->getTouches().empty());
}

TEST_CASE("touch.getTouchInvalidThrows") {
    auto* t = eve::touch::Touch::create();
    REQUIRE(t != nullptr);

    bool threw = false;
    try {
        (void)t->getTouch(999);
    } catch (const eve::Exception&) {
        threw = true;
    }
    CHECK(threw);
}
