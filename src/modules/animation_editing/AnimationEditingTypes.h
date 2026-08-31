#pragma once

#include "editing/EditableTarget.h"
#include "editing/EditingTargetOperations.h"

namespace eve::animation_editing {
using editing::CapabilityId;
using editing::Diagnostic;
using editing::DiagnosticSeverity;
using editing::DomainOperation;
using editing::EditRegion;
using editing::IDomainOperationTarget;
using editing::IDomainOperationTargetStaging;
using editing::IEditableTarget;
using editing::Revision;
using editing::RuleId;
using editing::StableId;
using editing::Status;
using editing::TargetDescriptor;
using editing::TargetId;
using editing::Value;
template <class T>
using EditorResult = editing::Result<T>;
using EditorStatus     = editing::Status;
using EditorValue      = editing::Value;
using EditorDiagnostic = editing::Diagnostic;
}  // namespace eve::animation_editing
