dofile("rig-data.nut");
persist atelierFields = [
    ["happy","Smile","Expression",0.0,1.0,0.0],
    ["angry","Angry brows","Expression",0.0,1.0,0.0],
    ["sad","Sad (binary)","Expression",0.0,1.0,0.0],
    ["surprised","Surprised","Expression",0.0,1.0,0.0],
    ["blink","Close eyelids","Expression",0.0,1.0,0.0],
    ["aa","Mouth / A","Expression",0.0,1.0,0.0],
    ["ih","Mouth / I","Expression",0.0,1.0,0.0],
    ["height","Overall height","Body",0.88,1.12,1.0],
    ["shoulders","Shoulder width","Body",0.85,1.15,1.0],
    ["waist","Waist width","Body",0.85,1.15,1.0],
    ["hips","Hip width","Body",0.85,1.15,1.0],
    ["legs","Leg length","Body",0.90,1.12,1.0],
    ["arms","Arm length","Body",0.90,1.10,1.0],
    ["head","Head size","Body",0.90,1.10,1.0],
    ["lightMidpoint","Light threshold","Surface",-0.5,0.5,0.0],
    ["lightSmoothness","Shadow softness","Surface",0.01,0.5,0.22],
    ["rimStrength","Rim highlight","Surface",0.0,0.8,0.12]
];
persist atelierPoses = [
    ["idle","Relaxed stand","user"], ["weight","Weight shift","move"],
    ["hips","Hands on hips","cubes"], ["wave","Wave hello","user"],
    ["think","Thinking","eye"], ["tpose","T-pose / inspect","cube"]
];
function atelierDefinition() {
    local parameters=[],items=[];
    foreach(f in atelierFields) parameters.append({id=f[0],min=f[3],max=f[4],["default"]=f[5]});
    foreach(c in atelierRigData.controls) foreach(axis in ["x","y","z","sx","sy"]) {
        local size=axis.len()>1;
        parameters.append({id="face_"+c.id+"_"+axis,min=size?0.82:-0.012,max=size?1.18:0.012,["default"]=size?1.0:0.0});
    }
    foreach(p in atelierPoses) items.append({id=p[0],slot="pose",parts=[],hides=[],excludes=[]});
    return {schema="eve.appearance.definition",version=1,profile="pixiv-female-bones-v2",
        parameters=parameters,drivers=[],slots=[{id="pose",["default"]="idle"}],items=items};
}
