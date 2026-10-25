// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "helper",
  "default": "local-logic",
  "module": "editing"
}
*/
// clang-format on

#include "zeroerr/unittest.h"

int main(int argc, char** argv) { return zeroerr::UnitTest().parseArgs(argc, const_cast<const char**>(argv)).run(); }
