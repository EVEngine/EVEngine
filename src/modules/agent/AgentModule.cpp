#include "agent/AgentModule.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include "agent/Balance.h"

namespace eve::agent {
Module_IMPL(Agent, new Agent());
void Agent::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Agent::create, false);
    expose(cls);
    balance::BalanceSearch::expose(table);
}
}  // namespace eve::agent
