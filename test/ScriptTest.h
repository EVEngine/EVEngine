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

  /**
   * @brief Calls one script test function and returns its boolean result.
   *
   * Prefer this over `vm.callFunc(vm.findFunc(name), vm).toBool()`: a raised script
   * error otherwise arrives as simplesquirrel's "Unknown squirrel runtime error"
   * (the engine's error hook records the context instead of filling simplesquirrel's
   * `runtimeException`), which hides the file, line and reason. This prints the
   * recorded context before returning false.
   *
   * @param name Name of the script function, which must take no arguments.
   * @return The function's boolean result, or false when it raised or returned a
   *         non-boolean.
   * @remarks A non-boolean return is reported but not converted, so a test that
   *       returns a string cannot silently pass.
   */
  bool callScript(const char* name) {
      try {
          const ssq::Object result = vm.callFunc(vm.findFunc(name), vm);
          if (result.getType() != ssq::Type::BOOL) {
              std::cerr << "[script] " << name << " returned " << result.getTypeStr();
              // Several scripts return a reason string instead of false; surface it.
              if (result.getType() == ssq::Type::STRING) std::cerr << ": " << result.toString();
              std::cerr << std::endl;
              return false;
          }
          if (!result.toBool()) std::cerr << "[script] " << name << " returned false" << std::endl;
          return result.toBool();
      } catch (const std::exception& error) {
          std::cerr << "[script] " << name << " raised: " << error.what() << "\n"
                    << eve::script::formatLastScriptError(vm.getHandle()) << std::endl;
          return false;
      }
  }

  const char* script;
  ssq::VM vm;
};
