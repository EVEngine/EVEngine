#pragma once
#include "rpg/RPGActor.h"
namespace eve::rpg {
/** @brief Encode the actual RPG adapter and its canonical lifecycle/modifier relationships; no callbacks. */
[[nodiscard]] Result<Value> encodeStatusState(const RPGActor::Statuses&       state,
                                              const attributes::AttributeSet& attributes);
/** @brief Prepare detached adapter facts using exact content, inheriting only the destination handle generation.
 * @param content Nullable synchronous borrow: null uses live effect definitions, non-null uses admitted content.
 * @ownership All input borrows end on return. No capability, entity, policy or event is published.
 */
[[nodiscard]] Result<RPGActor::Statuses> prepareStatusState(const Value&                    value,
                                                            const attributes::AttributeSet& attributes,
                                                            const RPGActor::Statuses&       destination,
                                                            const SkillContent*             content);
}  // namespace eve::rpg
