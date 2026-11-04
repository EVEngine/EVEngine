// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "helper",
  "default": "module-composition",
  "module": "physics"
}
*/
// clang-format on

// Hostless test executable has no PlatformEvent module. Reaching push is an error.
#include <stdexcept>
#include "platform_event/PlatformEvent.h"
namespace eve::platform_event {
Message::Message(const std::string& n, const std::vector<Variant>& a) : name(n), args(a) {}
Message::~Message() = default;
void PlatformEvent::push(Message* message) {
    delete message;
    throw std::logic_error("No platform event provider in force integration harness");
}
}  // namespace eve::platform_event
