#pragma once

#include "editing/EditingProperty.h"
#include "editor/EditorProtocol.h"
#include "editor/EditorResult.h"
#include "editor/EditorSelection.h"
#include "editor/EditorValue.h"

namespace eve::editor {

using PropertyType = eve::editing::PropertyType;
using PropertyFlag = eve::editing::PropertyFlag;
using eve::editing::operator|;
using eve::editing::operator&;
using eve::editing::hasPropertyFlag;
using NumericMetadata          = eve::editing::NumericMetadata;
using PropertyDescriptor       = eve::editing::PropertyDescriptor;
using PropertyDescriptorLookup = eve::editing::PropertyDescriptorLookup;
using PropertySchema           = eve::editing::PropertySchema;
using PropertyReadState        = eve::editing::PropertyReadState;
using PropertyReadResult       = eve::editing::PropertyReadResult;
using PropertySetMode          = eve::editing::PropertySetMode;
using IPropertyProvider        = eve::editing::IPropertyProvider;
using eve::editing::toPresentationDescriptor;
using eve::editing::toPresentationValue;
using eve::editing::validatePropertyValue;

/** @brief Compatibility spelling for conversion into the authoring value tree. */
inline EditorValue toEditorValue(const eve::Value& value) { return eve::editing::toEditingValue(value); }

}  // namespace eve::editor
