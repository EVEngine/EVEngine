local retained=[];
function retain(v){if(v==null)throw "pcg-ui-scaler: null object";retained.append(v);return v;}
function requireResult(r,name){if(!r.ok)throw "pcg-ui-scaler: "+name+" failed";return r.value;}

eve_init=function(){
    local settings=retain(eve.ParentScalerSettings());settings.partScreen=true;settings.maxHeight=500.0;
    local input=retain(eve.ParentScalerInput());input.hasCanvas=true;input.targetCount=2;input.canvasHeight=640.0;
    local state=retain(eve.ParentScalerState());local output=retain(eve.ParentScalerOutput());
    requireResult(eve.evaluateParentScaler(state,output,settings,input),"evaluate");
    if(!output.applyHeight||output.height!=500.0)throw "pcg-ui-scaler: incorrect clamped height";

    ui.setTheme("dark");ui.beginBuild();ui.beginWindow("Pcg Parent Scaler", "root");
    ui.sectionHeader("PART SCREEN MODE", "mode");
    ui.text("Canvas height", "canvas-label");ui.sameLine("canvas-line");ui.badge("640 px", "canvas-value");
    ui.text("Maximum height", "maximum-label");ui.sameLine("maximum-line");ui.badge("500 px", "maximum-value");
    ui.separator("result-separator");
    ui.textWrapped("The target panel follows canvas height and clamps to Pcg's configured maximum.",520.0,"description");
    ui.progress(0.78125,"height-progress","500 / 640");
    ui.beginCard("result-card");ui.sectionHeader("APPLIED OUTPUT", "result-title");
    ui.text("Two caller-owned Rect targets", "target-count");ui.badge("HEIGHT 500", "result-height");ui.end();
    ui.end();ui.mountBuildAs("pcg-parent-scaler");ui.setHostSize(620.0,output.height);
    ui.setHostPos(170.0,70.0,0.0,0.0);ui.setHostMovable(false);ui.setHostResizable(false);
    print("PCG_PARENT_SCALER_READY canvas=640 mode=part targets=2 height="+output.height+"\n");
};

eve_update=function(dt){};
eve_render=function(){gfx.clear();ui.beginFrameAndRender();};
