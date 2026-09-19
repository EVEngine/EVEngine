#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cassert>
#include <iostream>
#include <simplesquirrel/simplesquirrel.hpp>

#include "common/ScriptError.h"
#include "filesystem/Filesystem.h"
#include "mouse/Mouse.h"
#include "window/Window.h"

#define UnitSciptTest(name, content) \
    class name : public ScriptTest { \
    public: \
        name() : ScriptTest(content) {} \
    }

using namespace eve;

class ScriptTest {
public:
  ScriptTest(const char* script)
    : script(script), vm(2048, ssq::Libs::ALL) {
    expose(vm);
    ssq::Script s = vm.compileSource(script);
    vm.run(s);
  }

protected:
  static void expose(ssq::VM& vm) {
      // Match the engine's own VM configuration. A bare `ssq::VM` keeps
      // simplesquirrel's `defaultRuntimeErrorFunc` as the Squirrel error handler, and
      // that handler dereferences the callback stack without checking it -- a script
      // error raised at call setup reaches it whatever `sq_call`'s `raiseerror` says,
      // so a test VM without the engine hook can crash where a game never would.
      eve::script::installScriptErrorHook(vm.getHandle());
      ModuleManager::expose(vm);
  }

  const char* script;
  ssq::VM vm;
};
