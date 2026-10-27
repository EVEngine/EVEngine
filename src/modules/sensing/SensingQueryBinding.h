#pragma once
#include "sensing/Sensing.h"
namespace ssq {
class Table;
class Class;
}  // namespace ssq
namespace eve::sensing {
struct ScriptSensingWorld {
    explicit ScriptSensingWorld(SensingWorldHandleRef value) : reference(value) {}
    ~ScriptSensingWorld() noexcept { Sensing::release(reference).ignore("script sensing world proxy destruction"); }
    SensingWorldHandleRef reference;
};

/** @brief Registers spatial queries and owned environment plans at the Squirrel boundary. */
void exposeSensingQueries(ssq::Table& table, ssq::Class& world);
}  // namespace eve::sensing
