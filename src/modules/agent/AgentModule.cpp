#include "agent/AgentModule.h"
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::agent {
Module_IMPL(Agent, new Agent());
void Agent::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Agent::create, false);
    expose(cls);
}
}  // namespace eve::agent
