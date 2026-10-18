// Midnight District: one urban layout drives road baking and parcel clearance.
local seed = 20261017;
local objects = [], lights = [], signals = [], kit = {}, materials = {};
local camera = null, cube = null, elapsed = 0.0, orbit = false, view = 0, frame = 0, captured = false;
local nodes = [], segments = [], edges = [];
local localLightsOn = true;
local emissionShader = null;

function need(r, context) {
    if (!r.ok) throw context + ": " + r.status.summary;
    return "value" in r ? r.value : null;
}
function mat(name, r, g, b, metal=0.0, rough=0.7, glow=false) {
    local m = gfx.newMaterial();
    m.setTint(r,g,b,1.0); m.setMetallic(metal); m.setRoughness(rough);
    m.setCastShadow(!glow); m.setReceiveShadow(!glow);
    if (glow) {
        m.setShader(emissionShader);m.setShadingModel("custom");m.setReceiveLight(false);
        m.setSurfaceMode("transparent");m.setBlendMode("additive");m.setDepthWrite(false);
    }
    materials[name] <- m; return m;
}
function place(mesh, material, x,y,z, sx=1.0,sy=1.0,sz=1.0,yaw=0.0,shadow=true) {
    local o = eve.Renderable3D(); o.setMesh(mesh); o.setMaterial(material);
    o.setPosition(x,y,z); o.setScale(sx,sy,sz); o.setRotation(yaw,0.0,0.0);
    o.setCastShadow(shadow); o.setReceiveShadow(true); objects.append(o); return o;
}
function box(m,x,y,z,sx,sy,sz,yaw=0.0,shadow=true) {
    return place(cube,m,x,y,z,sx,sy,sz,yaw,shadow);
}
function model(name,m,x,y,z,sx=1.0,sy=1.0,sz=1.0,yaw=0.0,shadow=true) {
    return place(kit[name],m,x,y,z,sx,sy,sz,yaw,shadow);
}
function nodeAt(x,z) {
    foreach(i,n in nodes) if ((n.x-x)*(n.x-x)+(n.z-z)*(n.z-z)<0.0025) return i;
    nodes.append({x=x,z=z,degree=0}); return nodes.len()-1;
}
function streetGraph(mesh) {
    // mesh.urban emits its flat street quads first, at y=.01, then parcel tops at y=0.
    // Guard this representation explicitly rather than silently accepting a changed recipe.
    for(local i=0;i+3<mesh.getVertexCount();i+=4) {
        if (abs(mesh.getPositionY(i)-0.01)>0.0001) break;
        for(local j=0;j<4;++j)
            if(abs(mesh.getPositionY(i+j)-0.01)>0.0001) throw "urban street quad contract changed";
        local a=nodeAt((mesh.getPositionX(i)+mesh.getPositionX(i+3))*0.5,
                       (mesh.getPositionZ(i)+mesh.getPositionZ(i+3))*0.5);
        local b=nodeAt((mesh.getPositionX(i+1)+mesh.getPositionX(i+2))*0.5,
                       (mesh.getPositionZ(i+1)+mesh.getPositionZ(i+2))*0.5);
        if(a!=b) segments.append([a,b]);
    }
    if(segments.len()==0) throw "urban layout contains no street ribbons";
    // Co-generated access roads can end on another segment: split at all such endpoints.
    local unique={};
    foreach(s in segments) {
        local a=nodes[s[0]], b=nodes[s[1]], dx=b.x-a.x,dz=b.z-a.z, ll=dx*dx+dz*dz;
        local cuts=[];
        foreach(i,n in nodes) {
            local t=((n.x-a.x)*dx+(n.z-a.z)*dz)/ll;
            local px=a.x+t*dx,pz=a.z+t*dz;
            if(t>=-0.0001&&t<=1.0001&&(n.x-px)*(n.x-px)+(n.z-pz)*(n.z-pz)<0.0025)
                cuts.append({i=i,t=t});
        }
        cuts.sort(@(a,b) a.t<b.t?-1:(a.t>b.t?1:0));
        for(local i=1;i<cuts.len();++i) {
            local a=cuts[i-1].i,b=cuts[i].i;
            local key=(a<b?a:b)+":"+(a>b?a:b);
            if(a!=b&&!(key in unique)) {
                unique[key]<-true; edges.append([a,b]); nodes[a].degree++; nodes[b].degree++;
            }
        }
    }
}
function xyz(n) { return format("[%.6f,0.0,%.6f]",n.x,n.z); }
function snapshot() {
    local s="{\"schema\":\"eve.procgen.roadNetwork\",\"schemaVersion\":1,\"nodes\":[";
    foreach(i,n in nodes) {
        if(i>0)s+=",";
        s+=format("{\"id\":%d,\"x\":%.6f,\"y\":0.0,\"z\":%.6f,\"junctionRadius\":5.0,\"junctionControl\":3}",i+1,n.x,n.z);
    }
    s+="],\"edges\":[";
    foreach(i,e in edges) {
        if(i>0)s+=",";
        s+=format("{\"id\":%d,\"from\":%d,\"to\":%d,\"lanesForward\":1,\"lanesBackward\":1,",i+1,e[0]+1,e[1]+1);
        s+="\"points\":["+xyz(nodes[e[0]])+","+xyz(nodes[e[1]])+"],";
        s+="\"pointProfiles\":[{\"widthScale\":1.0,\"rollDegrees\":0.0,\"pathControl\":2},{\"widthScale\":1.0,\"rollDegrees\":0.0,\"pathControl\":2}],";
        s+=format("\"style\":{\"id\":%d,",i+1);
        s+="\"laneWidth\":3.2,\"curbWidth\":0.25,\"curbHeight\":0.18,\"sidewalkWidth\":1.65,\"sidewalkHeight\":0.16,";
        s+="\"deckThickness\":0.7,\"pierWidth\":1.35,\"pierDepth\":1.35,\"pierSpacing\":11.0,\"pierClearance\":1.5,";
        s+="\"markingWidth\":0.12,\"dashLength\":2.0,\"dashGap\":2.0,\"uvMeters\":8.0,\"speedLimitMps\":11.0,\"trafficPriority\":0,";
        s+="\"samplingPolicy\":1,\"samplingSpacingMeters\":2.0,\"samplingAngleDegrees\":45.0,\"widthInterpolation\":1},\"laneLinks\":[]}";
    }
    return s+"],\"laneLinks\":[]}";
}
function buildRoads() {
    local p=need(procgen.newParams(),"road params");
    need(p.setString("networkSnapshot",snapshot()),"road graph");
    foreach(k,v in {navigation=false,piers=false,placements=false,markings=true,junctions=true})
        need(p.setBool(k,v),"road option "+k);
    local cpu=need(procgen.buildMesh("mesh.roadNetwork",p),"road bake");
    if(cpu.getMeta("edges","")!=edges.len().tostring()||cpu.getMeta("nodes","")!=nodes.len().tostring())
        throw "Road runtime did not bake the supplied city graph; use a current SDK";
    for(local i=0;i<cpu.getGroupCount();++i) {
        local g=cpu.copyGroup(i); if(g==null||g.empty())continue;
        local name=cpu.getGroupName(i);
        local m=name=="asphalt"?materials.asphalt:(name=="markingYellow"?materials.yellow:
            (name=="marking"?materials.white:materials.paving));
        place(need(procgen.uploadMesh(g,gfx),"road upload"),m,0.0,0.0,0.0);
    }
    print("NIGHT_CITY_ROADS nodes="+nodes.len()+" edges="+edges.len()+" vertices="+cpu.getVertexCount()+"\n");
}
function spot(x,y,z,r,g,b,intensity,radius,shadow) {
    local l=eve.Light3D(); l.setType("spot"); l.setPosition(x,y,z);
    l.setDirection(0.0,-1.0,0.0); l.setColor(r,g,b,intensity); l.setRadius(radius);
    l.setSpotAngle(76.0); l.setSpotSoftness(0.5); l.setCastShadow(shadow);
    l.setShadowMethod("perspective"); l.setShadowStrength(0.85); lights.append(l);
}
function streetFurniture() {
    local shadowCount=0;
    foreach(e in edges) {
        local a=nodes[e[0]],b=nodes[e[1]],dx=b.x-a.x,dz=b.z-a.z,len=sqrt(dx*dx+dz*dz);
        dx/=len;dz/=len;
        for(local t=8.0;t<len-7.0;t+=19.0) foreach(side in [-1,1]) {
            local x=a.x+dx*t-dz*4.5*side,z=a.z+dz*t+dx*4.5*side;
            // Local lamp +X arm points inwards over the asphalt.
            local yaw=atan2(dx*side,dz*side);
            model("lamp",materials.metal,x,0.18,z,1.0,1.0,1.0,yaw);
            model("lamp-glow",materials.warm,x,0.18,z,1.0,1.0,1.0,yaw,false);
            spot(x+dz*side*1.65,6.55,z-dx*side*1.65,1.0,0.66,0.31,8.0,14.0,shadowCount<8);
            shadowCount++;
        }
        foreach(end in [0,1]) {
            local n=nodes[e[end]]; if(n.degree<3||len<14.0) continue;
            local sign=end==0?1.0:-1.0;
            local x=n.x+dx*sign*7.0-dz*4.2,z=n.z+dz*sign*7.0+dx*4.2;
            local yaw=atan2(dx*sign,dz*sign);
            model("signal",materials.metal,x,0.18,z,1.0,1.0,1.0,yaw);
            local lenses=[];
            foreach(h in [3.91,3.45,2.99]) lenses.append(model("lens",materials.off,x+dx*sign*0.17,h+0.18,z+dz*sign*0.17,1.0,1.0,1.0,yaw,false));
            signals.append({lenses=lenses,axis=abs(dx)>abs(dz)?0:1});
            // Crossing stripes sit outside the junction envelope, perpendicular to approach.
            for(local j=-3;j<=3;++j) {
                local cx=n.x+dx*sign*6.7-dz*j*0.82,cz=n.z+dz*sign*6.7+dx*j*0.82;
                box(materials.white,cx,0.026,cz,0.48,0.025,2.0,atan2(dx,dz),false);
            }
        }
    }
}
function clearParcel(g,id,x,z,w,d) {
    for(local zz=floor(z-d*0.5).tointeger();zz<=ceil(z+d*0.5).tointeger();++zz)
        for(local xx=floor(x-w*0.5).tointeger();xx<=ceil(x+w*0.5).tointeger();++xx) {
            local gx=xx+1,gz=zz+1;
            if(gx<0||gz<0||gx>=g.getWidth()||gz>=g.getHeight()||g.getDetail(gx,gz)!=id||g.getCell(gx,gz)!=2) return false;
        }
    return true;
}
function buildings(g) {
    local count=0;
    for(local i=0;i<g.getObjectCount();++i) {
        local x=g.getObjectX(i)-1.0,z=g.getObjectY(i)-1.0;
        local id=g.getDetail((x+1).tointeger(),(z+1).tointeger());
        if(id==0||!clearParcel(g,id,x,z,4.0,4.0))continue;
        local w=4.0,d=4.0;
        for(local j=0;j<22;++j) {
            if(clearParcel(g,id,x,z,w+1.0,d))w+=1.0;
            if(clearParcel(g,id,x,z,w,d+1.0))d+=1.0;
        }
        local h=18.0+((i*19+seed)%43),sx=(w-1.0)/12.0,sz=(d-1.0)/12.0;
        local suffix=h>=36.0?"-high":"",sy=h/(h>=36.0?49.5:27.1);
        model("tower"+suffix,i%3==0?materials.stone:materials.building,x,0.22,z,sx,sy,sz);
        model("dark"+suffix,materials.glass,x,0.22,z,sx,sy,sz,0.0,false);
        model("lit"+suffix,i%4==0?materials.cool:materials.windows,x,0.22,z,sx,sy,sz,0.0,false);
        // Architectural crown and recessed illuminated entrance.
        local crown=i%4==0?materials.cool:materials.warm;
        local cw=(w-1.0)*0.79,cd=(d-1.0)*0.79,cy=(h>=36.0?47.9:25.5)*sy+0.25;
        foreach(side in [-1,1]) {
            box(crown,x+side*cw*0.5,cy,z,0.08,0.08,cd,0.0,false);
            box(crown,x,cy,z+side*cd*0.5,cw,0.08,0.08,0.0,false);
        }
        box(materials.warm,x,1.8,z+(d-1.0)*0.46,2.2,2.7,0.08,0.0,false);
        spot(x,4.2,z+(d-1.0)*0.47,1.0,0.62,0.27,3.5,7.0,count<4);
        // Window spill lights illuminate neighbouring pavement rather than only glowing pixels.
        if(count<12) {
            local l=eve.Light3D(); l.setType("point"); l.setPosition(x,3.0,z+(d-1.0)*0.48);
            l.setColor(1.0,0.59,0.23,1.5);l.setRadius(7.0);l.setCastShadow(false);lights.append(l);
        }
        local facade=eve.Light3D();facade.setType("point");
        facade.setPosition(x,h*0.48,z+(d-1.0)*0.46+0.2);
        if(i%4==0)facade.setColor(0.25,0.65,1.0,2.0);else facade.setColor(1.0,0.59,0.23,2.0);
        facade.setRadius(14.0);facade.setCastShadow(false);lights.append(facade);
        count++;
    }
    print("NIGHT_CITY_BUILDINGS count="+count+" parcels="+g.getObjectCount()+"\n");
}
function setView() {
    if(view==0) {camera.setEye(183.0,132.0,204.0);camera.setTarget(77.0,12.0,57.0);}
    if(view==1) {camera.setEye(150.0,12.0,145.0);camera.setTarget(65.0,15.0,45.0);}
    if(view==2) {camera.setEye(78.0,205.0,61.0);camera.setTarget(80.0,0.0,60.0);}
    print("NIGHT_CITY_VIEW id="+view+"\n");
}
function listenControls() {
    asyncOnEvent("keypressed").then(function(raw) {
        local k=raw.tolower();
        if(k=="1"||k=="2"||k=="3") {view=k.tointeger()-1;orbit=false;setView();}
        if(k=="o")orbit=!orbit;
        if(k=="l") {
            localLightsOn=!localLightsOn;foreach(l in lights)l.setEnabled(localLightsOn);
            print("NIGHT_CITY_LOCAL_LIGHTS enabled="+localLightsOn+"\n");
        }
        listenControls();
    });
}
eve_init=function() {
    cube=gfx.newMeshCube(1.0);
    emissionShader=need(gfx.loadMeshShaderSpv("assets/emission.vert.spv","assets/emission.frag.spv"),"emission shader");
    mat("asphalt",0.075,0.10,0.14,0.2,0.28);mat("paving",0.38,0.43,0.49,0.05,0.8);
    mat("white",0.75,0.79,0.73);mat("yellow",0.94,0.62,0.16);
    mat("metal",0.055,0.075,0.09,0.65,0.32);mat("building",0.14,0.21,0.29,0.35,0.45);
    mat("stone",0.28,0.29,0.32,0.1,0.65);mat("glass",0.018,0.044,0.075,0.65,0.18);
    mat("ground",0.035,0.055,0.08);mat("warm",1.0,0.61,0.20,0.0,0.5,true);
    mat("windows",1.0,0.57,0.22,0.0,0.5,true);mat("cool",0.18,0.6,1.0,0.0,0.5,true);
    mat("off",0.018,0.025,0.03);mat("red",1.0,0.015,0.005,0.0,0.5,true);
    mat("amber",1.0,0.42,0.015,0.0,0.5,true);mat("green",0.015,1.0,0.25,0.0,0.5,true);
    local surfaceParams=need(procgen.newParams(),"asphalt params");
    need(surfaceParams.setSeed(seed),"asphalt seed");need(surfaceParams.setSize(256,256),"asphalt size");
    local surface=need(procgen.generatePbrMaterial("pbr.asphalt",surfaceParams),"asphalt surface");
    materials.asphalt.setAlbedoTexture(gfx.newTexture(surface.getAlbedo(),true,true));
    materials.asphalt.setNormalTexture(gfx.newTexture(surface.getNormal(),true,true));
    materials.asphalt.setTint(0.45,0.52,0.62,1.0);surface.destroy();
    foreach(name in ["tower","lit","dark","tower-high","lit-high","dark-high","lamp","lamp-glow","signal","lens"]) {
        local data=model3d.newModelDataFromFile("assets/"+name+".obj");
        if(data==null||data.getMeshCount()!=1)throw "Missing SCAD kit mesh: "+name;
        local sample=model3d.createRenderable(gfx,data,0);sample.setVisible(false);
        kit[name]<-sample.getMesh();objects.append(sample);
    }
    camera=eve.Camera3D();camera.setUp(0.0,1.0,0.0);camera.setFov(48.0);
    camera.setClipPlanes(0.3,700.0);camera.setAmbient(0.055,0.075,0.12);
    camera.setBloom(0.20,1.1);camera.setExposure(-0.4);camera.setActive(true);setView();
    gfx.setBackgroundColor(0.012,0.023,0.055,1.0);
    gfx.setDirectionalLight(-0.6,-1.0,-0.3,0.18,0.27,0.46);
    local rc=gfx.getRenderControl();foreach(f in ["shadow","clustered","gbuffer","gbufferAlbedo","ao","aa"])rc.enable(f);
    gfx.setMsaaSamples(4);rc.enable("msaa");rc.compile();
    box(materials.ground,80.0,-0.38,60.0,184.0,0.6,144.0);
    local p=need(procgen.newParams(),"urban params");need(p.setSeed(seed),"urban seed");
    need(p.setFloat("landWidth",160.0),"land width");need(p.setFloat("landHeight",120.0),"land height");
    need(p.setInt("targetParcels",28),"parcel count");need(p.setFloat("minParcelArea",100.0),"parcel area");
    need(p.setFloat("streetWidth",11.0),"street envelope");need(p.setFloat("extrude",0.0),"flat layout");
    need(p.setBool("optimize",true),"urban optimizer");
    local urban=need(procgen.buildMesh("mesh.urban",p),"urban layout");streetGraph(urban);buildRoads();
    local grid=need(procgen.generate("urban.parcels",p),"urban parcel occupancy");streetFurniture();buildings(grid);
    listenControls();
    print("NIGHT_CITY_READY seed="+seed+" lights="+lights.len()+" signals="+signals.len()+" objects="+objects.len()+"\n");
};
eve_update=function(dt) {
    elapsed+=dt;frame++;
    local forward=(keyboard.isDown("w")?1.0:0.0)-(keyboard.isDown("s")?1.0:0.0);
    local sideways=(keyboard.isDown("d")?1.0:0.0)-(keyboard.isDown("a")?1.0:0.0);
    local vertical=(keyboard.isDown("e")?1.0:0.0)-(keyboard.isDown("q")?1.0:0.0);
    if(forward!=0.0||sideways!=0.0||vertical!=0.0) {
        orbit=false;
        local ex=camera.getEyeX(),ey=camera.getEyeY(),ez=camera.getEyeZ();
        local tx=camera.getTargetX(),ty=camera.getTargetY(),tz=camera.getTargetZ();
        local dx=tx-ex,dz=tz-ez,len=sqrt(dx*dx+dz*dz);
        if(len>0.01) {
            dx/=len;dz/=len;
            local mx=(dx*forward-dz*sideways)*dt*22.0,mz=(dz*forward+dx*sideways)*dt*22.0,my=vertical*dt*16.0;
            camera.setEye(ex+mx,ey+my,ez+mz);camera.setTarget(tx+mx,ty+my,tz+mz);
        }
    }
    if(orbit){camera.setEye(80.0+cos(elapsed*0.07)*173.0,110.0,60.0+sin(elapsed*0.07)*173.0);camera.setTarget(80.0,14.0,60.0);}
    local phase=elapsed%20.0;
    foreach(s in signals) {
        local t=(phase+s.axis*10.0)%20.0;
        local active=t<7.0?2:(t<9.0?1:0);
        for(local i=0;i<3;++i)s.lenses[i].setMaterial(i==active?(i==0?materials.red:(i==1?materials.amber:materials.green)):materials.off);
    }
    if(frame>=90&&!captured) {
        if(gfx.saveFramePng("night-city.png")) {captured=true;print("NIGHT_CITY_FRAME night-city.png\n");}
    }
};
eve_render=function(){gfx.clear();gfx.render3D();};
