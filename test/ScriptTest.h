#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <cassert>
#include <iostream>
#include <simplesquirrel/simplesquirrel.hpp>

#include "window/Window.h"
#include "filesystem/Filesystem.h"
#include "mouse/Mouse.h"

#define UnitSciptTest(name, content) \
    class name : public ScriptTest { \
    public: \
        name() : ScriptTest(content) {} \
    }

using namespace eve;

class ScriptTest {
public:
  ScriptTest(const char* script)
    : vm(2048, ssq::Libs::ALL), script(script) {
    expose(vm);
    ssq::Script s = vm.compileSource(script);
    vm.run(s);
  }

protected:
  static void expose(ssq::VM& vm) {
    ModuleManager::expose(vm);
  }

  const char* script;
  ssq::VM vm;
};
