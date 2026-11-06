// Presentation recipe only: topology, facade assignment and occupancy belong to HouseGen.
// All geometry/UV generation and CPU mesh composition use native Procgen recipes.
function houseNeed(result) {
    if (!result.ok) throw "native house: " + result.status.summary;
    return result.value;
}

class RiversideHouseAppearance {
    meshes = null;
    primitives = null;
    counts = null;
    constructor() { meshes={}; primitives={}; counts={walls=0,windows=0,doors=0,tiles=0}; }

    function primitive(kind, w, h, d) {
        local key=format("%s/%.6f/%.6f/%.6f",kind,w,h,d);
        if (!(key in primitives)) {
            local p=houseNeed(procgen.newParams());
            p.setFloat("width",w.tofloat()); p.setFloat("height",h.tofloat()); p.setFloat("depth",d.tofloat());
            p.setFloat("uvScale",1.0); p.setFloat("thickness",0.06);
            primitives[key] <- houseNeed(procgen.buildMesh("prototype."+kind,p));
        }
        return primitives[key];
    }
    function add(role,kind,x,y,z,w,h,d,yaw=0.0) {
        if (w<=0 || h<=0 || d<=0) throw "Invalid house primitive extent";
        if (!(role in meshes)) {
            local p=houseNeed(procgen.newParams());
            local mesh=houseNeed(procgen.buildMesh("prototype.cube",p));
            mesh.clear(); meshes[role] <- mesh;
        }
        if (!meshes[role].appendTransformed(primitive(kind,w,h,d),x.tofloat(),y.tofloat(),z.tofloat(),yaw.tofloat(),1.0,1.0,1.0))
            throw "Native mesh composition failed";
    }
    function box(role,x,y,z,w,h,d,yaw=0.0) { add(role,"cube",x,y,z,w,h,d,yaw); }

    // A facade is one native layout face. Its details never reach adjacent bays.
    function facade(id,x,y,z,rotation,fh) {
        local nx=rotation==90 ? 1.0 : (rotation==270 ? -1.0 : 0.0);
        local nz=rotation==180 ? 1.0 : (rotation==0 ? -1.0 : 0.0);
        local ax=-nz, az=nx;
        local px=x+nx*0.40, pz=z+nz*0.40;
        local panel=function(role,u,v,out,w,h,d) {
            box(role,px+ax*u+nx*out,y+v,pz+az*u+nz*out,w,h,d,rotation.tofloat());
        }.bindenv(this);
        ++counts.walls;
        // Four facade-local members, never a slab spanning the whole building.
        foreach (u in [-0.46,0.46]) panel("timber",u,0,0,0.06,fh,0.10);
        foreach (v in [0.0,fh-0.075]) panel("timber",0,v,0,0.96,0.075,0.10);
        if (id=="door") {
            ++counts.doors;
            panel("plaster",-0.36,0,-0.015,0.24,fh,0.075);
            panel("plaster",0.36,0,-0.015,0.24,fh,0.075);
            panel("plaster",0,1.65,-0.015,0.48,fh-1.65,0.075);
            panel("door",0,0,0.015,0.46,1.65,0.055);
            foreach(u in [-0.27,0.27]) panel("timber",u,0,0.055,0.06,1.75,0.08);
            panel("timber",0,1.69,0.055,0.60,0.07,0.08);
            foreach(u in [-0.15,-0.05,0.05,0.15]) panel("timber",u,0.06,0.05,0.012,1.50,0.016);
            panel("brass",0.15,0.78,0.075,0.04,0.06,0.04);
            panel("roof_dark",0,1.83,0.015,0.86,0.075,0.12);
            // The step stays inside the layout cell; roof eaves alone overhang.
            panel("stone",0,0,0.015,0.72,0.06,0.10);
        } else if (id=="window") {
            ++counts.windows;
            panel("plaster",0,0,-0.015,0.90,0.73,0.075);
            panel("plaster",0,1.56,-0.015,0.90,fh-1.56,0.075);
            foreach(u in [-0.355,0.355]) panel("plaster",u,0.73,-0.015,0.19,0.83,0.075);
            panel("glass",0,0.74,0,0.51,0.80,0.035);
            foreach(u in [-0.28,0,0.28]) panel("timber",u,0.71,0.045,0.035,0.87,0.045);
            foreach(v in [0.71,1.13,1.56]) panel("timber",0,v,0.045,0.59,0.035,0.045);
            foreach(u in [-0.37,0.37]) for(local v=0.75;v<1.55;v+=0.13)
                panel("door",u,v,0.04,0.10,0.09,0.04);
            panel("stone",0,0.64,0.025,0.67,0.07,0.10);
        } else panel("plaster",0,0,-0.015,0.90,fh,0.075);
    }

    function roof(layout,width,depth,floors,seed) {
        local y=0.25+floors*layout.getFloorHeight();
        local cx=(width-1)*0.5, cz=(depth-1)*0.5;
        local w=width+0.48, run=depth*0.5+0.24, rise=1.45;
        local kind=layout.getRoofStyle();
        if (kind=="flat") {
            box("roof_dark",cx,y,cz,width+0.16,0.10,depth+0.16);
            foreach(s in [-1,1]) {
                box("stone",cx+s*(width*0.5-0.06),y,cz,0.12,0.28,depth);
                box("stone",cx,y,cz+s*(depth*0.5-0.06),width,0.28,0.12);
            }
            return;
        }
        local sides=kind=="gable" ? [-1,1] : [1];
        if(kind=="shed") run=depth+0.48;
        local rows=ceil(run/0.27).tointeger(), cols=ceil(w/0.38).tointeger();
        local dz=run/rows;
        foreach(s in sides) {
            local startZ=kind=="gable" ? cz-s*run : cz-run*0.5;
            local yaw=s==1 ? 0.0 : 180.0;
            // A native ramp closes the roof volume, including gables/end walls.
            add("roof_dark","ramp",cx,y,startZ+s*run*0.5,w,rise,run,yaw);
            foreach(edge in [-1,1])
                add("plaster","ramp",cx+edge*(width*0.5-0.08),y+rise*0.24/run,
                    startZ+s*(run+0.24)*0.5,0.08,rise*(run-0.24)/run,run-0.24,yaw);
            for(local row=0;row<rows;++row) {
                local cuts=[0.0];
                for(local col=0;col<=cols;++col) {
                    local u=(col+(row%2 ? 0.5 : 0.0))/cols;
                    if(u>0 && u<1) cuts.append(u);
                }
                cuts.append(1.0);
                for(local col=0;col<cuts.len()-1;++col) {
                    local x0=cx-w*0.5+w*cuts[col], x1=cx-w*0.5+w*cuts[col+1];
                    local shade=procgen.deriveSeed(seed,format("tile/%d/%d/%d",s,row,col)) & 15;
                    local role=shade<2 ? "roof_light" : (shade>13 ? "roof_dark" : "roof");
                    // Non-overlapping courses plus a down-slope lip: no coplanar overlap.
                    local z=startZ+s*(row+0.5)*dz;
                    local tileY=y+rise*row/rows+0.045;
                    box(role,(x0+x1)*0.5,tileY,z,x1-x0-0.012,0.035,dz-0.009);
                    add(role,"ramp",(x0+x1)*0.5,tileY+0.035,z,x1-x0-0.012,rise/rows,dz-0.009,yaw);
                    ++counts.tiles;
                }
            }
            box("timber",cx,y-0.04,startZ,w,0.10,0.10);
        }
        if(kind=="gable") for(local i=0;i<cols;++i)
            box("roof_dark",cx-w*0.5+(i+0.5)*w/cols,y+rise+0.07,cz,w/cols-0.01,0.08,0.14);
        else box("timber",cx,y+rise,cz+run*0.5,w,0.09,0.09);
        box("brick",cx+width*0.27,y+rise*0.35,cz-0.35,0.42,rise*0.65+0.48,0.40);
        box("stone",cx+width*0.27,y+rise+0.48,cz-0.35,0.52,0.12,0.50);
        box("roof_dark",cx+width*0.27,y+rise+0.60,cz-0.35,0.30,0.015,0.28);
    }
}

function prepareNativeHouse(kit,settings) {
    foreach(key in ["width","depth","floors","seed"])
        if(typeof settings[key]!="integer") throw "House dimensions/seed must be integers";
    if(settings.width<3 || settings.width>10 || settings.depth<3 || settings.depth>4 ||
       settings.floors<1 || settings.floors>3) throw "Unsupported house presentation size";
    if(["gable","shed","flat"].find(settings.roof)==null) throw "Unsupported native roof";
    if(["terracotta","slate","moss"].find(settings.palette)==null) throw "Unsupported palette";
    local req=kit.newRequest();
    req.setPlot(settings.width,settings.depth); req.setFloors(settings.floors);
    req.setModuleSize(1.0); req.setFloorHeight(2.125); req.setFootprint("rectangle");
    req.setRoof(settings.roof); req.setEntrance("south"); req.setSeed(settings.seed);
    local layout=kit.newLayout(); houseNeed(kit.generate(req,layout));
    local grid=houseNeed(procgen.newGrid(1,1)); houseNeed(layout.writeFootprintGrid(grid));
    local appearance=RiversideHouseAppearance();
    local maxFloor=0;
    for(local i=0;i<layout.getInstanceCount();++i) {
        local id=layout.getInstanceComponentId(i);
        local x=layout.getInstanceX(i).tofloat(), z=layout.getInstanceY(i).tofloat();
        local level=layout.getInstanceZ(i), rot=layout.getInstanceRotationDeg(i);
        local y=0.25+level*layout.getFloorHeight();
        if(id=="foundation") appearance.box("stone",x,0,z,1.0,0.25,1.0);
        else if(id=="floor") appearance.box("timber",x,y-0.06,z,0.98,0.06,0.98);
        else if(id=="stairs") {
            appearance.box("stone",x,0,z,1.0,0.25,1.0);
            appearance.add("timber","stairs",x,y,z,0.75,layout.getFloorHeight(),0.9);
        } else if(id=="roof") { if(level>maxFloor) maxFloor=level; }
        else if(["wall","window","door"].find(id)!=null)
            appearance.facade(id,x,y,z,rot,layout.getFloorHeight());
        else throw "No presentation for component: "+id;
    }
    appearance.roof(layout,grid.getWidth(),grid.getHeight(),maxFloor,procgen.deriveSeed(settings.seed,"roof.color"));
    return {layout=layout,grid=grid,appearance=appearance,settings=settings,parts=[]};
}

function houseColors(palette) {
    local colors={plaster=[0.88,0.79,0.61],timber=[0.25,0.15,0.09],stone=[0.51,0.54,0.55],
        door=[0.48,0.28,0.12],glass=[0.13,0.35,0.40],brass=[0.85,0.63,0.25],brick=[0.62,0.36,0.25]};
    local roof=palette=="terracotta" ? [0.61,0.26,0.15] : (palette=="slate" ? [0.24,0.34,0.42] : [0.29,0.39,0.24]);
    if(palette=="slate") { colors.plaster=[0.76,0.80,0.79]; colors.door=[0.30,0.38,0.41]; }
    if(palette=="moss") { colors.plaster=[0.79,0.76,0.59]; colors.timber=[0.30,0.23,0.12]; }
    colors.roof <- roof;
    colors.roof_light <- [roof[0]*1.12,roof[1]*1.12,roof[2]*1.12];
    colors.roof_dark <- [roof[0]*0.78,roof[1]*0.78,roof[2]*0.78];
    return colors;
}

nativeHouseKit <- null;
nativeHouses <- [];
activeNativeHouse <- -1;
houseLot <- null;

function nativeHouseOccupies(candidate,tx,tz) {
    local x=tx-houseLot.x, z=tz-houseLot.z;
    return x>=0 && z>=0 && x<candidate.grid.getWidth() && z<candidate.grid.getHeight() &&
           candidate.grid.getCell(x,z)!=0;
}

function selectNativeHouse(index) {
    if(index==activeNativeHouse) return;
    // Refuse a larger footprint while a character stands inside its future occupancy.
    local candidate=nativeHouses[index];
    foreach(a in actors) foreach(dx in [-6.0,6.0]) foreach(dz in [-6.0,6.0])
        if(nativeHouseOccupies(candidate,floor((a.x+dx)/32.0).tointeger(),floor((a.z+dz)/32.0).tointeger()))
            return;
    foreach(i,house in nativeHouses) foreach(part in house.parts) part.setVisible(i==index);
    activeNativeHouse=index;
    houseParts=candidate.parts;
    print("HD2D_NATIVE_HOUSE preset="+candidate.settings.name+" roof="+candidate.layout.getRoofStyle()+
          " parts="+candidate.parts.len()+" windows="+candidate.appearance.counts.windows+"\n");
}

function initializeNativeHouses() {
    local minX=riversideData.width, minZ=riversideData.height;
    for(local z=0;z<riversideData.height;++z) for(local x=0;x<riversideData.width;++x)
        if(riversideData.layers.raised[z*riversideData.width+x]==10) {
            if(x<minX) minX=x; if(z<minZ) minZ=z;
        }
    if(minX==riversideData.width) throw "Missing house placement lot";
    houseLot={x=minX,z=minZ};
    nativeHouseKit=eve.HouseGen();
    houseNeed(nativeHouseKit.loadComponentsFromFile("assets/house/components.json"));
    local candidates=[];
    // All layout and CPU geometry validation happens before any render publication.
    foreach(settings in riversideHousePresets) {
        local candidate=prepareNativeHouse(nativeHouseKit,settings);
        for(local z=0;z<candidate.grid.getHeight();++z) for(local x=0;x<candidate.grid.getWidth();++x) {
            local tx=minX+x, tz=minZ+z;
            if(candidate.grid.getCell(x,z)!=0 && (tx>=riversideData.width || tz>=riversideData.height ||
               riversideData.layers.raised[tz*riversideData.width+tx]!=10)) throw "House exceeds reserved lot";
        }
        if(candidate.appearance.counts.doors!=1) throw "House must have exactly one entrance";
        candidates.append(candidate);
    }
    local woodParams=houseNeed(procgen.newParams());
    woodParams.setSize(128,128); woodParams.setSeed(procgen.deriveSeed(7,"material.wood"));
    woodParams.setString("tone","oak"); woodParams.setInt("pixelSize",2);
    local wood=houseNeed(procgen.generateTexture("tex.floor.wood",woodParams,gfx));
    local sampler=hd2d.newLook(); sampler.applyPixelSampler(gfx,wood);
    foreach(candidate in candidates) {
        local colors=houseColors(candidate.settings.palette);
        foreach(role,mesh in candidate.appearance.meshes) {
            // Borrowed GPU meshes belong to Graphics; renderables retained for scene lifetime.
            local gpu=houseNeed(procgen.uploadMesh(mesh,gfx));
            local part=eve.Renderable3D(); part.setVisible(false); part.setMesh(gpu);
            part.setPosition((minX+0.5)*32.0,0.0,(minZ+0.5)*32.0);
            part.setScale(32.0,32.0,32.0);
            local c=colors[role]; part.setTint(c[0],c[1],c[2],1.0);
            if(role=="timber" || role=="door") part.setTexture(wood);
            part.setRoughness(role=="glass" ? 0.25 : 0.95);
            part.setCastShadow(true); part.setReceiveShadow(true);
            candidate.parts.append(part);
        }
    }
    nativeHouses=candidates;
    selectNativeHouse(0);
}

function listenNativeHouseKeys() {
    asyncOnEvent("keypressed").then(function(raw) {
        local key=raw.tolower();
        if(key=="4" || key=="5" || key=="6" || key=="7") selectNativeHouse(key.tointeger()-4);
        if(key=="v") cameraYaw+=0.65;
        listenNativeHouseKeys();
    });
}
