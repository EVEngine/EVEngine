// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "compile-contract",
  "default": "module-composition",
  "module": "crowd"
}
*/
// clang-format on

#include "common/CrashHandler.h"
#include "zeroerr/unittest.h"

int main(int argc, char** argv) {
#if defined(EVENGINE_WINDOWS) || defined(_WIN32)
    eve::installCrashHandler();
#endif
    zeroerr::UnitTest test;
    test.parseArgs(argc, const_cast<const char**>(argv));
    return test.run();
}
