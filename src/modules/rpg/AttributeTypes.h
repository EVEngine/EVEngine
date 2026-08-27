#pragma once

/**
 * @file AttributeTypes.h
 * @brief RPG compatibility names for the canonical attributes data model.
 *
 * RPG used to own a second AttributeValue/modifier implementation. These
 * aliases intentionally leave the public include path available while making
 * attributes::AttributeSet and its calculation function the only source of
 * attribute state and arithmetic.
 */

#include "attributes/AttributeSet.h"

namespace eve::rpg {

using AttributeId        = ::eve::attributes::AttributeId;
using ModifierId         = ::eve::attributes::ModifierId;
using SourceId           = ::eve::attributes::SourceId;
using AttributeOperation = ::eve::attributes::AttributeOperation;
using AttributeModifier  = ::eve::attributes::AttributeModifier;
using AttributeValue     = ::eve::attributes::AttributeValue;
using AttributeOpTable   = ::eve::attributes::AttributeOperationRegistry;

/** @brief RPG exposes the canonical attributes computation without a duplicate overload. */
using ::eve::attributes::computeAttributeValue;

}  // namespace eve::rpg
