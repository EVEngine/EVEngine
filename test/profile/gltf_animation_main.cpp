// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "compile-contract",
  "default": "module-composition",
  "module": "common"
}
*/
// clang-format on

#include "zeroerr/unittest.h"

int main(int argc, char** argv) {
    zeroerr::UnitTest test;
    test.parseArgs(argc, const_cast<const char**>(argv));
    return test.run();
}
