#pragma once

// Compatibility-only facade. Canonical editing authority APIs live in the editing module.
#include "editing/EditingAuthority.h"
#include "editing/EditingTargetOperations.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorTarget.h"

namespace eve::editor {

using IDomainOperationTarget = editing::IDomainOperationTarget;
using IDomainOperationTargetStaging = editing::IDomainOperationTargetStaging;
using IEditAuthority = editing::IEditAuthority;
using LocalWorldAuthority = editing::LocalWorldAuthority;
using ReadOnlyAuthority = editing::ReadOnlyAuthority;

}  // namespace eve::editor
