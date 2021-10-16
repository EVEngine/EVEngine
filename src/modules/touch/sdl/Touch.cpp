
#include "touch/sdl/Touch.h"

#include <SDL2/SDL_events.h>

#include <algorithm>

#include "common/Exception.h"

using TouchInfo = eve::touch::Touch::TouchInfo;

namespace eve::touch::sdl {

const std::vector<TouchInfo> &Touch::getTouches() const { return touches; }

const TouchInfo &Touch::getTouch(int64_t id) const {
    for (const auto &touch : touches) {
        if (touch.id == id) return touch;
    }

    throw Exception("Invalid active touch ID: %d", id);
}

void Touch::onEvent(uint32_t eventtype, const TouchInfo &info) {
    auto compare = [&](const TouchInfo &touch) -> bool { return touch.id == info.id; };

    switch (eventtype) {
        case SDL_FINGERDOWN:
            touches.erase(std::remove_if(touches.begin(), touches.end(), compare), touches.end());
            touches.push_back(info);
            break;
        case SDL_FINGERMOTION: {
            for (TouchInfo &touch : touches) {
                if (touch.id == info.id) touch = info;
            }
            break;
        }
        case SDL_FINGERUP: touches.erase(std::remove_if(touches.begin(), touches.end(), compare), touches.end()); break;
        default: break;
    }
}

}  // namespace eve::touch::sdl