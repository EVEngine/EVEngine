#pragma once

namespace ssq {
class Table;
}

namespace eve::animation {

/** @brief Register AnimClip's authoring and playback-neutral Squirrel surface. */
void exposeAnimClipBindings(ssq::Table& table);
/** @brief Register the owned scalar animation curve library and checked loader. */
void exposeAnimCurveLibraryBindings(ssq::Table& table);
/** @brief Register the synchronous MotionMatcher Squirrel surface. */
void exposeMotionMatcherBindings(ssq::Table& table);
/** @brief Register the owning AnimInertializer Squirrel surface. */
void exposeAnimInertializerBindings(ssq::Table& table);

/** @brief Register playback, sampling and event inspection for AnimPlayer. */
void exposeAnimPlayerBindings(ssq::Table& table);

}  // namespace eve::animation
