#pragma once

#include <cstdlib>
#include <string_view>

namespace eve::test {
// Opt-in human viewing; automated assertions always run before this hold.
inline bool manualPreviewEnabled() {
    const char* value = std::getenv("EVENGINE_TEST_MANUAL_PREVIEW");
    return value && std::string_view(value) == "1";
}
}  // namespace eve::test
