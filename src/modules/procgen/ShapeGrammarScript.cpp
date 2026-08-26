#include "procgen/ShapeGrammarScript.h"

#include "procgen/ShapeGrammar.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::procgen {

void exposeShapeGrammar(ssq::Table& table) {
    auto grammar = table.addClass<ShapeGrammar>(
        "ProcgenShapeGrammar",
        std::function<ShapeGrammar*()>([]() -> ShapeGrammar* { return nullptr; }), true);
    grammar.addFunc("clear", &ShapeGrammar::clear);
    grammar.addFunc("addModule", &ShapeGrammar::addModule);
    grammar.addFunc("removeModule", &ShapeGrammar::removeModule);
    grammar.addFunc("hasModule", &ShapeGrammar::hasModule);
    grammar.addFunc("getModuleCount", &ShapeGrammar::getModuleCount);
    grammar.addFunc("getModuleSymbol", &ShapeGrammar::getModuleSymbol);
    grammar.addFunc("getVariantCount", &ShapeGrammar::getVariantCount);
    grammar.addFunc("getVariantAsset", &ShapeGrammar::getVariantAsset);
    grammar.addFunc("getVariantLength", &ShapeGrammar::getVariantLength);
    grammar.addFunc("validate", &ShapeGrammar::validate);
    grammar.addFunc("generate", &ShapeGrammar::generate);
    grammar.addFunc("getError", &ShapeGrammar::getError);
    grammar.addFunc("debugReport", &ShapeGrammar::debugReport);
}

}  // namespace eve::procgen
