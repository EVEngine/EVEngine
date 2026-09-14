#include <SDL2/SDL.h>
#include "mouse/Mouse.h"
#include "platform_event/PlatformEvent.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

TEST_CASE("mouse.wheel snapshot accumulates precise flipped input and resets per pump") {
    auto* mouse  = eve::mouse::Mouse::create();
    auto* events = eve::platform_event::PlatformEvent::create();
    events->pump();
    SDL_Event event{};
    event.type    = SDL_MOUSEWHEEL;
    event.wheel.x = 2;
    event.wheel.y = 3;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    event.wheel.preciseX = 2.25f;
    event.wheel.preciseY = 3.5f;
#endif
    REQUIRE(SDL_PushEvent(&event) == 1);
    event.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    event.wheel.x         = 1;
    event.wheel.y         = 1;
#if SDL_VERSION_ATLEAST(2, 0, 18)
    event.wheel.preciseX = 0.5f;
    event.wheel.preciseY = 1.25f;
#endif
    REQUIRE(SDL_PushEvent(&event) == 1);
    events->pump();
#if SDL_VERSION_ATLEAST(2, 0, 18)
    CHECK_EQ(mouse->getWheelX(), 1.75f);
    CHECK_EQ(mouse->getWheelY(), 2.25f);
    CHECK_EQ(mouse->getWheelY(), 2.25f);
#else
    CHECK_EQ(mouse->getWheelX(), 1.f);
    CHECK_EQ(mouse->getWheelY(), 2.f);
    CHECK_EQ(mouse->getWheelY(), 2.f);
#endif
    events->pump();
    CHECK_EQ(mouse->getWheelX(), 0.f);
    CHECK_EQ(mouse->getWheelY(), 0.f);
}
