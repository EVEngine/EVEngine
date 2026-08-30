#pragma once

#include "editing/EditableTarget.h"
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
#include "editing/EditingSelection.h"
#include "editing/EditingTargetOperations.h"

namespace eve::scene_editing {

using editing::CapabilityId;
using editing::Diagnostic;
using editing::DiagnosticSeverity;
using editing::DomainOperation;
using editing::EditRegion;
using editing::IDomainOperationTarget;
using editing::IDomainOperationTargetStaging;
using editing::IEditableTarget;
using editing::IPropertyProvider;
using editing::ObjectId;
using editing::PropertyDescriptor;
using editing::PropertyFlag;
using editing::PropertyPath;
using editing::PropertyReadResult;
using editing::PropertyReadState;
using editing::PropertySchema;
using editing::PropertySetMode;
using editing::PropertyType;
using editing::Revision;
using editing::RuleId;
using editing::SelectionDomain;
using editing::SelectionItem;
using editing::SelectionSnapshot;
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

}  // namespace eve::scene_editing
