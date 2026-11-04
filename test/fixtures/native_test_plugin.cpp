// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "helper",
  "default": "local-logic",
  "module": "common"
}
*/
// clang-format on

#include "common/Export.h"
#include "common/Module.h"

#include <string>

namespace eve::plugins::test {

class NativeTestPlugin : public Module {
public:
    Module_REG(NativeTestPlugin);

};

Module_IMPL(NativeTestPlugin, new NativeTestPlugin());

void NativeTestPlugin::expose(ssq::Table&) {}

void NativeTestPlugin::expose(ssq::Class&) {}

}  // namespace eve::plugins::test

EVE_PLUGIN_EXPORT int eve_plugin_init(void) { return 0; }
