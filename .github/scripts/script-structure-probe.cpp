// Minimal exception contract probe: run in Node before the full browser build.
#include <iostream>
#include <string>
#include "common/ScriptStructure.hpp"

int main() {
    std::string source;
    for (int i = 0; i < 2049; ++i) source += "local value" + std::to_string(i) + " = 0;\n";
    const auto result = eve::script::analyzeScriptStructure(source);
    if (result.diagnostics.size() != 1 || result.diagnostics.front() != "Script binding budget exceeded" ||
        !result.bindings.empty()) {
        std::cerr << "Structural analysis did not catch and contain the binding budget exception\n";
        return 1;
    }
    std::cout << "OK: structural analysis caught and contained the budget exception\n";
}
