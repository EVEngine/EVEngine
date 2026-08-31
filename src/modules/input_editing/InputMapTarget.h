#pragma once
#include "editing/EditingAuthority.h"
#include "editing/EditingProperty.h"
#include "editing/EditableTarget.h"
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace eve::input_editing {
using CapabilityId = editing::CapabilityId;
using DiagnosticSeverity = editing::DiagnosticSeverity;
using DomainOperation = editing::DomainOperation;
using EditRegion = editing::EditRegion;
using EditorDiagnostic = editing::Diagnostic;
template<class T> using EditorResult = editing::Result<T>;
using EditorStatus = editing::Status;
using EditorValue = editing::Value;
using IDomainOperationTarget = editing::IDomainOperationTarget;
using IDomainOperationTargetStaging = editing::IDomainOperationTargetStaging;
using IEditableTarget = editing::IEditableTarget;
using IPropertyProvider = editing::IPropertyProvider;
using ObjectId = editing::ObjectId;
using PropertyDescriptor = editing::PropertyDescriptor;
using PropertyFlag = editing::PropertyFlag;
using PropertyPath = editing::PropertyPath;
using PropertyReadResult = editing::PropertyReadResult;
using PropertyReadState = editing::PropertyReadState;
using PropertySchema = editing::PropertySchema;
using PropertySetMode = editing::PropertySetMode;
using PropertyType = editing::PropertyType;
using Revision = editing::Revision;
using RuleId = editing::RuleId;
using SelectionSnapshot = editing::SelectionSnapshot;
using TargetDescriptor = editing::TargetDescriptor;
using TargetId = editing::TargetId;
using editing::validatePropertyValue;
}
namespace eve::input_editing{
struct InputActionValue{ObjectId id;std::string name;std::string kind="button";};
struct InputBindingValue{ObjectId id,action;std::string device="keyboard",control;double scale=1,deadzone=0;bool invert=false;};
class InputMapTarget final:public virtual IEditableTarget,public IDomainOperationTarget,public IDomainOperationTargetStaging,public IPropertyProvider{
public:explicit InputMapTarget(std::string id);const std::string&targetId()const override{return id_;}unsigned long long revision()const override{return revision_;}EditRegion dirtyRegion()const override{return dirty_;}void clearDirtyRegion()override{dirty_.clear();}TargetDescriptor describe()const override;void*queryCapability(const CapabilityId&)override;EditorResult<void>applyDomainOperation(const DomainOperation&)override;std::unique_ptr<IDomainOperationTarget>cloneDomainState()const override;EditorResult<void>commitDomainState(std::unique_ptr<IDomainOperationTarget>)override;eve::Result<eve::Revision>currentRevision(const SelectionSnapshot&)const override;PropertySchema schema(const SelectionSnapshot&)const override;PropertyReadResult read(const SelectionSnapshot&,const PropertyPath&)const override;EditorResult<DomainOperation>makeSet(const SelectionSnapshot&,const PropertyPath&,const EditorValue&,PropertySetMode)const override;EditorResult<DomainOperation>makeReset(const SelectionSnapshot&,const PropertyPath&)const override;EditorResult<DomainOperation>makeCreateAction(InputActionValue)const;EditorResult<DomainOperation>makeDeleteAction(const ObjectId&)const;EditorResult<DomainOperation>makeCreateBinding(InputBindingValue)const;EditorResult<DomainOperation>makeDeleteBinding(const ObjectId&)const;const std::vector<InputActionValue>&actions()const{return actions_;}const std::vector<InputBindingValue>&bindings()const{return bindings_;}std::vector<EditorDiagnostic>validate()const;EditorValue snapshotValue()const;EditorResult<void>loadSnapshot(const EditorValue&);
private:bool matches(const SelectionSnapshot&)const;EditorValue contentValue()const;EditorResult<DomainOperation>replacement(EditorValue,std::string={})const;std::string id_;Revision revision_=1;EditRegion dirty_;std::vector<InputActionValue>actions_;std::vector<InputBindingValue>bindings_;};
struct InputControlSample{std::string device,control;double value=0;};
/** @brief Deterministically evaluates raw device samples against an authored action map. */
class InputMapEvaluator{public:EditorResult<std::map<std::string,double>>evaluate(const InputMapTarget&,const std::vector<InputControlSample>&)const;};
/** @brief Captures the first intentional control movement for a rebind UI. */
class InputBindingCapture{public:void begin(std::string device={});void cancel();bool active()const{return active_;}std::optional<InputControlSample>feed(InputControlSample);private:bool active_=false;std::string device_;};
} // namespace eve::input_editing
