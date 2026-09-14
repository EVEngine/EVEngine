#include "ui/PcgDraggableWindow.h"
#include "common/SquirrelBinding.h"
#include <simplesquirrel/simplesquirrel.hpp>
#include <algorithm>
#include <cmath>
namespace eve::ui {
namespace { Result<void> bad(const char* message) { return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,message,"ui.pcgDraggableWindow")); } }
Result<void> PcgDraggableWindow::configure(float x,float y,float width,float height,float screenWidth,float screenHeight,float scale) {
 if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||!std::isfinite(screenWidth)||!std::isfinite(screenHeight)||!std::isfinite(scale)||width<=0||height<=0||screenWidth<width||screenHeight<height||scale<=0)return bad("invalid draggable-window geometry");
 x_=resetX_=x;y_=resetY_=y;width_=width;height_=height;screenWidth_=screenWidth;screenHeight_=screenHeight;scale_=scale;configured_=true;bringToFront_=false;return Result<void>::success();
}
Result<void> PcgDraggableWindow::drag(float dx,float dy) {
 if(!configured_)return bad("draggable window is not configured");if(!std::isfinite(dx)||!std::isfinite(dy))return bad("pointer delta must be finite");
 float x=x_+dx/scale_,y=y_+dy/scale_;x_=-std::clamp(std::abs(x),width_*.5f,screenWidth_-width_*.5f);y_=std::clamp(std::abs(y),height_*.5f,screenHeight_-height_*.5f);return Result<void>::success();
}
void PcgDraggableWindow::pointerDown(bool middle) noexcept { bringToFront_=true;if(middle&&configured_){x_=resetX_;y_=resetY_;} }
PcgUiRequestStatus PcgDraggableWindow::consumeBringToFront() noexcept { auto value=bringToFront_?PcgUiRequestStatus::Requested:PcgUiRequestStatus::None;bringToFront_=false;return value; }
void exposePcgDraggableWindowBindings(ssq::Table& table){auto cls=table.addClass("PcgDraggableWindow",ssq::Class::Ctor<PcgDraggableWindow()>());auto vm=table.getHandle();cls.addFunc("configure",[vm](PcgDraggableWindow*s,float x,float y,float w,float h,float sw,float sh,float scale){return eve::script::projectResult(vm,s->configure(x,y,w,h,sw,sh,scale));});cls.addFunc("drag",[vm](PcgDraggableWindow*s,float x,float y){return eve::script::projectResult(vm,s->drag(x,y));});cls.addFunc("pointerDown",[](PcgDraggableWindow*s,bool middle){s->pointerDown(middle);});cls.addFunc("consumeBringToFront",[](PcgDraggableWindow*s){return s->consumeBringToFront()==PcgUiRequestStatus::Requested;});cls.addFunc("getX",[](const PcgDraggableWindow*s){return s->getX();});cls.addFunc("getY",[](const PcgDraggableWindow*s){return s->getY();});}
}
