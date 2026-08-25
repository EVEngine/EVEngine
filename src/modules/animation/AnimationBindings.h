#pragma once

namespace ssq {
class Table;
}

namespace eve::animation {

/** @brief Register AnimClip's authoring and playback-neutral Squirrel surface. */
void exposeAnimClipBindings(ssq::Table& table);

}  // namespace eve::animation
