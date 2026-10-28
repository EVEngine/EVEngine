#include "action/ActionModule.h"

#include "action/ActionScriptBindings.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::action {

Module_IMPL(Action, new Action());

void Action::expose(ssq::Table& table) { exposeActionScriptBindings(table); }

void Action::expose(ssq::Class& cls) { exposeActionScriptBindings(cls); }

}  // namespace eve::action
