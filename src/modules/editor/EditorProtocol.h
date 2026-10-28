#pragma once
#include "editing/EditingProtocol.h"
#include "editor/EditorCommandTypes.h"
#include "editor/EditorHostProfile.h"
#include "editor/EditorIds.h"
#include "editor/EditorResult.h"
#include "editor/EditorValue.h"
namespace eve::editor {
using Revision              = eve::editing::Revision;
using ObjectRefValue        = eve::editing::ObjectRefValue;
using EditorContextSnapshot = eve::editing::ContextSnapshot;
using DomainOperation       = eve::editing::DomainOperation;
using CommandRequest        = eve::editing::CommandRequest;
using CommandPlan           = eve::editing::CommandPlan;
using TransactionState      = eve::editing::TransactionState;
using ActionOrigin          = eve::editing::ActionOrigin;
using TransactionSpec       = eve::editing::TransactionSpec;
using TransactionReceipt    = eve::editing::TransactionReceipt;
using AuthorityPlan         = eve::editing::AuthorityPlan;
}  // namespace eve::editor
