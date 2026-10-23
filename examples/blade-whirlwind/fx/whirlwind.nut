// Reusable presentation component. Required providers: gfx, procgen, eve.Image.
// All methods run on the game/render thread. No callbacks, weapon pointers or clocks.
local PI=3.14159265358979;
// One inert material anchors closed native entities until scene teardown.
// The exported module must outlive the native scene, just like its gfx provider.
local closedMaterial=null;
local bounded=function(v,lo,hi,name) {
    if((typeof v!="float" && typeof v!="integer") || v!=v || v<lo || v>hi)
        throw "whirlwind: invalid "+name;
    return v.tofloat();
}
local need=function(r,name) { if(!r.ok) throw "whirlwind "+name+": "+r.status.summary; return r.value; }
local clamp=function(v,a,b) { return v<a?a:(v>b?b:v); }
local copyValue=null;
copyValue=function(v) {
    if(typeof v=="array") { local a=[]; foreach(x in v) a.push(copyValue(x)); return a; }
    if(typeof v=="table") { local a={}; foreach(k,x in v) a[k]<-copyValue(x); return a; }
    return v;
}
local validate=function(source) {
    if(typeof source!="table") throw "whirlwind: definition must be a table";
    local d=copyValue(source);
    local names=["schema","version","id","height","radius","spin","scroll","travel","windup","sustain","fade",
        "opacity","color","edge","radiusCurve","scaleCurve","blades","turns","ribbonWidth","segments","seed","mask","shader"];
    foreach(k,v in d) if(names.find(k)==null) throw "whirlwind: unknown definition field "+k;
    foreach(k in names) if(!(k in d)) throw "whirlwind: missing definition field "+k;
    if(d.schema!="eve.example.whirlwind" || typeof d.version!="integer" || d.version!=1) throw "whirlwind: unsupported definition schema/version";
    foreach(k in ["id","mask","shader"]) if(typeof d[k]!="string" || d[k].len()==0) throw "whirlwind: empty "+k;
    foreach(k in ["height","radius"]) d[k]=bounded(d[k],0.05,20.0,k);
    foreach(k in ["windup","sustain","fade"]) d[k]=bounded(d[k],0.01,20.0,k);
    d.spin=bounded(d.spin,-30.0,30.0,"spin"); d.scroll=bounded(d.scroll,-10.0,10.0,"scroll");
    d.travel=bounded(d.travel,0.0,30.0,"travel"); d.opacity=bounded(d.opacity,0.0,1.0,"opacity");
    d.turns=bounded(d.turns,0.1,5.0,"turns"); d.ribbonWidth=bounded(d.ribbonWidth,0.01,0.4,"ribbonWidth");
    foreach(k in ["blades","segments","seed"]) if(typeof d[k]!="integer") throw "whirlwind: integer required for "+k;
    bounded(d.blades,1,12,"blades"); bounded(d.segments,16,256,"segments"); bounded(d.seed,0,1000000,"seed");
    foreach(k in ["color","edge","radiusCurve","scaleCurve"]) {
        if(typeof d[k]!="array" || d[k].len()!=(k=="color"||k=="edge"?3:4)) throw "whirlwind: invalid "+k;
        foreach(i,v in d[k]) d[k][i]=bounded(v,0.0,k=="color"||k=="edge"?1.0:2.0,k);
    }
    return d;
}
local curve=function(provider,values) {
    local path=need(provider.newSplinePath(),"create spline");
    need(path.setKind("catmullRom"),"curve interpolation");
    foreach(i,v in values) need(path.addPoint(v,i/3.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0),"curve key");
    local out=[];
    for(local i=0;i<65;++i) out.push(clamp(need(path.evaluateResult(i/64.0),"curve sample").getX(),0.0,2.0));
    return out;
}
local sample=function(values,t) {
    local f=clamp(t,0.0,1.0)*64.0, i=f.tointeger();
    if(i>=64) return values[64];
    return values[i]+(values[i+1]-values[i])*(f-i);
}
local makeMesh=function(graphics,d,radii) {
    local pos=[],normal=[],uv=[],indices=[];
    local vertex=function(x,y,z,u,v) {
        pos.extend([x,y,z]); normal.extend([0.0,1.0,0.0]); uv.extend([u,v]);
    };
    // Four long tapered helical blades. Geometry is uploaded once during prepare.
    for(local blade=0;blade<d.blades;++blade) {
        local offset=pos.len()/3;
        for(local i=0;i<=d.segments;++i) {
            local t=i.tofloat()/d.segments, a=t*d.turns*2*PI+blade*2*PI/d.blades;
            local r=sample(radii,t), taper=sin(PI*t);
            local w=d.ribbonWidth*(0.14+0.86*pow(taper,0.45));
            vertex(cos(a)*r,t-w*0.5,sin(a)*r,t*2.0,0.0);
            vertex(cos(a)*r,t+w*0.5,sin(a)*r,t*2.0,1.0);
        }
        for(local i=0;i<d.segments;++i) {
            local a=offset+i*2; indices.extend([a,a+1,a+2,a+1,a+3,a+2]);
        }
    }
    // Broken ground-level crescents reinforce the attack's footprint.
    for(local ring=0;ring<3;++ring) {
        local offset=pos.len()/3;
        for(local i=0;i<=48;++i) {
            local t=i/48.0, a=t*PI*1.35+ring*2.0*PI/3.0;
            local r=0.75+ring*0.17, w=0.075*sin(t*PI);
            vertex(cos(a)*(r-w),0.025,sin(a)*(r-w),t,0.0);
            vertex(cos(a)*(r+w),0.025,sin(a)*(r+w),t,1.0);
        }
        for(local i=0;i<48;++i) { local a=offset+i*2; indices.extend([a,a+1,a+2,a+1,a+3,a+2]); }
    }
    // Deterministic mesh particles: named debris stream, independent of gameplay RNG.
    for(local i=0;i<30;++i) {
        local a=i*2.399963+d.seed*0.001;
        local r=0.75+(i%7)*0.095, y=0.12+(i%11)*0.078;
        local x=cos(a)*r,z=sin(a)*r, s=0.009+(i%4)*0.003;
        local offset=pos.len()/3;
        vertex(x-s,y,z,0.0,2.0); vertex(x+s,y+0.045,z,0.5,2.5); vertex(x+s,y,z+s,1.0,3.0);
        indices.extend([offset,offset+1,offset+2]);
    }
    local mesh=graphics.newMeshFromArrays(pos,normal,uv,pos.len()/3,indices,indices.len());
    if(mesh==null) throw "whirlwind: mesh upload unavailable";
    return mesh;
}
local poseValue=function(pose) {
    if(typeof pose!="table") throw "whirlwind: pose must be a table";
    foreach(k in ["x","y","z","dx","dy","dz"]) {
        if(!(k in pose)) throw "whirlwind: missing pose "+k;
        bounded(pose[k],-100000.0,100000.0,k);
    }
    local n=sqrt(pose.dx*pose.dx+pose.dy*pose.dy+pose.dz*pose.dz);
    if(n<0.00001) throw "whirlwind: direction must be nonzero";
    return {x=pose.x.tofloat(),y=pose.y.tofloat(),z=pose.z.tofloat(),dx=pose.dx/n,dy=pose.dy/n,dz=pose.dz/n};
}
local Instance=class {
    _definition=null; _curve=null; _shader=null; _material=null; _object=null; _closedMaterial=null;
    _pose=null; _age=0.0; _lastAttack=-1; _state="idle"; _cancelAt=-1.0; _closed=false;
    _height=1.0; _radius=1.0; _spin=1.0; _travel=1.0;
    _durationScale=1.0; _distance=0.0; _cancelOpacity=1.0;
    constructor(graphics,d,mesh,texture,scaleCurve,neutral) {
        _closedMaterial=neutral;
        _definition=copyValue(d); _curve=scaleCurve;
        _height=d.height; _radius=d.radius; _spin=d.spin; _travel=d.travel;
        _shader=need(graphics.loadMeshShaderSpv("",d.shader),"load shader");
        foreach(k in ["time","scroll","red","green","blue","edgeRed","edgeGreen","edgeBlue"]) _shader.declareFloat(k);
        _shader.sendFloat("scroll",d.scroll);
        foreach(i,k in ["red","green","blue"]) _shader.sendFloat(k,d.color[i]);
        foreach(i,k in ["edgeRed","edgeGreen","edgeBlue"]) _shader.sendFloat(k,d.edge[i]);
        _material=graphics.newMaterial(); _material.setShader(_shader); _material.setShadingModel("custom");
        _material.setAlbedoTexture(texture); _material.setSurfaceMode("transparent");
        _material.setBlendMode("alpha"); _material.setDepthWrite(false); _material.setDoubleSided(true);
        _material.setCastShadow(false); _material.setReceiveShadow(false); _material.setReceiveLight(false);
        _object=eve.Renderable3D(); _object.setMesh(mesh); _object.setMaterial(_material); _object.setVisible(false);
    }
    /** @brief Trigger one attack. @return started, busy, duplicate or closed; never steals an active instance. */
    function trigger(attackId,pose,durationScale) {
        if(_closed) return "closed";
        if(typeof attackId!="integer" || attackId<0) throw "whirlwind: nonnegative integer attack ID required";
        local p=poseValue(pose); // Validate fully before mutating observable state.
        bounded(durationScale,0.1,5.0,"durationScale");
        if(attackId<=_lastAttack) return "duplicate";
        if(_state!="idle" && _state!="finished") return "busy";
        _lastAttack=attackId; _pose=p; _durationScale=durationScale;
        _age=0.0; _distance=0.0; _cancelAt=-1.0; _cancelOpacity=1.0; _state="windup";
        advance(0.0); return "started";
    }
    /** @brief Set an owning pose snapshot; no link to a potentially destroyed weapon is retained. */
    function setPose(pose) { if(_closed) throw "whirlwind: instance closed"; _pose=poseValue(pose); }
    /** @brief Cheap live controls, validated together and published atomically. */
    function setControls(height,radius,spin,travel) {
        bounded(height,0.05,20.0,"height"); bounded(radius,0.05,20.0,"radius");
        bounded(spin,-30.0,30.0,"spin"); bounded(travel,0.0,30.0,"travel");
        if(_closed) throw "whirlwind: instance closed";
        _height=height; _radius=radius; _spin=spin; _travel=travel;
    }
    /** @brief Begin a fade without resetting age. No effect if already fading or inactive. */
    function cancel() {
        if(_cancelAt<0.0 && (_state=="windup" || _state=="active")) {
            _cancelAt=_age;
            _cancelOpacity=_state=="windup"?clamp(_age/_definition.windup,0.0,1.0):1.0;
        }
    }
    /** @brief Hide immediately; retains the fixed scene allocation for reuse. */
    function stop() { if(!_closed) { _object.setVisible(false); _state="finished"; } }
    /** @brief Terminal idempotent deactivation. Scene resources live until native scene teardown. */
    function close() {
        if(!_closed) {
            stop();
            // Native setters require non-null instances. Swap the observer to the
            // module's inert material before this instance's material can be collected.
            _object.setMaterial(_closedMaterial);
            _closed=true; _state="closed";
        }
    }
    function getState() { return _state; }
    function getAge() { return _age; }
    function getAttackId() { return _lastAttack; }
    function isVisible() { return _object.getVisible(); }
    function getPosition() {
        if(_pose==null) return null;
        return {x=_pose.x+_pose.dx*_distance,y=_pose.y+_pose.dy*_distance,z=_pose.z+_pose.dz*_distance};
    }
    /** @brief Advance injected seconds exactly; dt=0 freezes; no asset allocation/upload or provider lookup. */
    function advance(dt) {
        bounded(dt,0.0,3600.0,"dt");
        if(_closed || _state=="idle" || _state=="finished") return;
        local previous=_age;
        _age+=dt/_durationScale;
        local d=_definition, fadeStart=_cancelAt>=0.0?_cancelAt:d.windup+d.sustain;
        local activeEnd=clamp(fadeStart-d.windup,0.0,d.sustain);
        _distance+=(clamp(_age-d.windup,0.0,activeEnd)-clamp(previous-d.windup,0.0,activeEnd))*_travel;
        local end=fadeStart+d.fade;
        if(_age>=end) { _age=end; stop(); return; }
        local opacity=1.0;
        if(_age>=fadeStart) { _state="fading"; opacity=(1.0-(_age-fadeStart)/d.fade)*_cancelOpacity; }
        else if(_age<d.windup) { _state="windup"; opacity=_age/d.windup; }
        else _state="active";
        local scale=sample(_curve,_age/(d.windup+d.sustain+d.fade));
        scale=clamp(scale,0.001,2.0);
        // Compose orientation(local +Y -> direction) with local spin, then Y-X-Z Euler.
        local yaw=atan2(_pose.dx,_pose.dz), pitch=acos(clamp(_pose.dy,-1.0,1.0)), spin=_age*_spin;
        local cy=cos(yaw),sy=sin(yaw),cp=cos(pitch),sp=sin(pitch),cs=cos(spin),ss=sin(spin);
        local py=atan2(cy*ss+sy*cp*cs,-sy*ss+cy*cp*cs);
        local px=asin(clamp(sp*cs,-1.0,1.0)), pz=atan2(sp*ss,cp);
        local p=getPosition();
        _object.setPosition(p.x,p.y,p.z); _object.setRotation(py,px,pz);
        _object.setScale(_radius*scale,_height*scale,_radius*scale);
        _object.setTint(1.0,1.0,1.0,d.opacity*opacity); _object.setVisible(true);
        _shader.sendFloat("time",_age);
    }
}
local Prepared=class {
    _instances=null; _definition=null; _closed=false;
    constructor(graphics,provider,definition,capacity,neutral) {
        bounded(capacity,1,8,"capacity");
        if(typeof capacity!="integer") throw "whirlwind: capacity must be integer";
        local d=validate(definition); _definition=d;
        local radii=curve(provider,d.radiusCurve), scales=curve(provider,d.scaleCurve);
        // Resolve all assets before publishing native renderable instances.
        local image=eve.Image().newImageDataFromFile(d.mask);
        if(image==null) throw "whirlwind: mask unavailable: "+d.mask;
        local texture=graphics.newTexture(image,false,false), mesh=makeMesh(graphics,d,radii);
        _instances=[];
        try { for(local i=0;i<capacity;++i) _instances.push(Instance(graphics,d,mesh,texture,scales,neutral)); }
        catch(error) { foreach(instance in _instances) instance.close(); throw error; }
    }
    /** @brief Borrow an instance owned by this prepared pool, valid until pool close or scene teardown. */
    function instance(index) {
        if(_closed) throw "whirlwind: pool closed";
        if(typeof index!="integer" || index<0 || index>=_instances.len()) throw "whirlwind: invalid instance index";
        return _instances[index];
    }
    function advance(dt) { foreach(instance in _instances) instance.advance(dt); }
    function close() { if(!_closed) { foreach(instance in _instances) instance.close(); _closed=true; } }
}
/** @brief Prepare a fixed pool; expensive: spline sampling, mesh upload and one shader/material per instance.
 * @cost One-time asset and GPU work proportional to blades*segments and capacity; amortized across all triggers.
 * @ownership Caller owns the pool; native scene owns renderables and Graphics owns uploaded GPU resources.
 * @throws Invalid definition or unavailable resources. Required providers must outlive the pool.
 */
return { prepare=function(graphics,provider,definition,capacity) {
    if(closedMaterial==null) closedMaterial=graphics.newMaterial();
    return Prepared(graphics,provider,definition,capacity,closedMaterial);
} };
