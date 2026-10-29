#include "rpg/balance/RPGBalanceModule.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "rpg/balance/RPGBalance.h"
namespace eve::rpg::balance {
Module_IMPL(RPGBalance, new RPGBalance());
void RPGBalance::expose(ssq::Class& cls) { cls.addFunc("getName", &RPGBalance::getName); }
void RPGBalance::expose(ssq::Table& table) {
    auto cls = table.addClass(name, RPGBalance::create, false);
    expose(cls);
    RPGBalanceSuite::expose(table);
}

}  // namespace eve::rpg::balance
