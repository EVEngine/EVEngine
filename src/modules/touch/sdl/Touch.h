#pragma once

#include "touch/Touch.h"


namespace eve::touch::sdl
{

/** @brief SDL touch backend; touch state is updated from the event pump. */
class Touch : public eve::touch::Touch
{
public:
    /** @brief Binds this backend into the SDL event sink. */
    Touch();
    /** @brief Releases active touch tracking and clears the event-sink owner. */
    ~Touch() override;

    /** @brief @copydoc eve::touch::Touch::getTouches */
    const std::vector<TouchInfo> &getTouches() const override;
    /** @brief @copydoc eve::touch::Touch::getTouch */
    const TouchInfo &getTouch(int64_t id) const override;

    /** @brief Updates touch state from an SDL event conversion (event module). */
    void onEvent(uint32_t eventtype, const TouchInfo &info);

private:

	// All current touches.
	std::vector<TouchInfo> touches;

}; // Touch

} // eve::touch::sdl
