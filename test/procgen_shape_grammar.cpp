#include "procgen/ShapeGrammar.h"

#include <zeroerr.hpp>

using namespace eve::procgen;

TEST_CASE("procgen.shapeGrammar.expandsGroupsAndFillRepetition") {
    ShapeGrammar grammar;
    CHECK(grammar.addModule("A", "wall-a", 2.f, 1.f));
    CHECK(grammar.addModule("A", "wall-b", 2.f, 1.f));
    CHECK(!grammar.addModule("A", "wrong-length", 3.f, 1.f));
    CHECK(grammar.addModule("B", "post", 1.f, 1.f));
    CHECK(grammar.validate("A,[B,A]*"));

    PointSet spline;
    spline.add(0.f, 0.f, 0.f);
    spline.add(10.f, 0.f, 0.f);
    PointSet* first  = grammar.generate("A,[B,A]*", &spline, 42, false);
    PointSet* second = grammar.generate("A,[B,A]*", &spline, 42, false);
    REQUIRE(bool(first));
    REQUIRE(bool(second));
    CHECK_EQ(first->getCount(), 5);
    CHECK_EQ(first->getStringAttribute(0, "module", ""), std::string("A"));
    CHECK_EQ(first->getStringAttribute(1, "module", ""), std::string("B"));
    CHECK_EQ(first->getStringAttribute(2, "module", ""), std::string("A"));
    CHECK_EQ(first->getX(0), 1.f);
    CHECK_EQ(first->getX(1), 2.5f);
    CHECK_EQ(first->getX(2), 4.f);
    CHECK_EQ(first->getYaw(0), 0.f);
    CHECK_EQ(first->getFloatAttribute(1, "length", 0.f), 1.f);
    for (int i = 0; i < first->getCount(); ++i)
        CHECK_EQ(first->getStringAttribute(i, "asset", ""),
                 second->getStringAttribute(i, "asset", ""));
    CHECK(grammar.debugReport().find("symbols=5") != std::string::npos);

    delete second;
    delete first;
}

TEST_CASE("procgen.shapeGrammar.supportsPlusExactAndIncompleteModes") {
    ShapeGrammar grammar;
    grammar.addModule("A", "wall", 2.f, 1.f);
    grammar.addModule("B", "post", 1.f, 1.f);
    PointSet spline;
    spline.add(0.f, 0.f, 0.f);
    spline.add(10.f, 0.f, 0.f);

    PointSet* plus = grammar.generate("[A,B]+", &spline, 1, false);
    REQUIRE(bool(plus));
    CHECK_EQ(plus->getCount(), 6);
    PointSet* exact = grammar.generate("A3,B2", &spline, 1, false);
    REQUIRE(bool(exact));
    CHECK_EQ(exact->getCount(), 5);

    PointSet shortSpline;
    shortSpline.add(0.f, 0.f, 0.f);
    shortSpline.add(5.f, 0.f, 0.f);
    CHECK(!grammar.generate("A3", &shortSpline, 1, false));
    CHECK(grammar.getError().find("does not fit") != std::string::npos);
    PointSet* partial = grammar.generate("A3", &shortSpline, 1, true);
    REQUIRE(bool(partial));
    CHECK_EQ(partial->getCount(), 2);

    delete partial;
    delete exact;
    delete plus;
}

TEST_CASE("procgen.shapeGrammar.reportsSyntaxAndUnknownModules") {
    ShapeGrammar grammar;
    grammar.addModule("A", "wall", 1.f, 1.f);
    CHECK(!grammar.validate("[A,A"));
    CHECK(grammar.getError().find("unterminated") != std::string::npos);
    CHECK(!grammar.validate("A,Missing"));
    CHECK(grammar.getError().find("unknown module") != std::string::npos);
    CHECK(!grammar.validate(""));
}
