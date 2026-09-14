#pragma once

/**
 * @file MotionScriptBindings.h
 * @brief Squirrel GC wrappers for Motion builder / handle / sequence.
 */

namespace ssq {
class Table;
class Class;
}

namespace eve::animation {

/** @brief Register Motion / MotionSequence script types and Animation factories. */
void exposeMotionScriptBindings(ssq::Table &table, ssq::Class &animationClass);

}  // namespace eve::animation
