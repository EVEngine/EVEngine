// Disposable presentation of canonical snapshots. No placement or save state lives here.
// Asset models are cached presentation; roof membership and support live in the world.
persist villageCamera = null
persist villageCube = null
persist villageModels = {}
persist villageSolids = []
persist villageGhosts = []
persist villageGrid = []
persist view3d = true
persist cutaway = true
persist viewDirty = true
persist ghostDirty = true

function villageBox(pool, index, x, y, z, sx, sy, sz, yaw, r, g, b, alpha) {
    if(index == pool.len()) {
        local item = eve.Renderable3D();
        item.setMesh(villageCube);pool.push(item);
    }
    local item=pool[index];item.setMesh(villageCube);
    item.setPosition(x.tofloat(),y.tofloat(),z.tofloat());item.setScale(sx.tofloat(),sy.tofloat(),sz.tofloat());item.setYaw(yaw.tofloat());
    item.setTint(r.tofloat(),g.tofloat(),b.tofloat(),alpha.tofloat());item.setVisible(view3d);
}
function villageLoadModels() {
    foreach(key in ["wall-window","chair","post","roof-gable"]) {
        local data=model3d.newModelDataFromFile("assets/"+key+".obj");
        if(data.getMeshCount()!=1)throw "Expected one mesh for "+key;
        local source=model3d.createRenderable(gfx,data,0);
        villageModels[key] <- source.getMesh();source.setVisible(false);
        // Hidden sources and pooled draw entities are owned by the scene until shutdown.
    }
}
function villageHideAfter(pool, count) {
    for(local i=count;i<pool.len();i++)pool[i].setVisible(false);
}
function villageProject(parts, pool, ghost) {
    local count=0,selectedIds={},openedIds={};
    if(!ghost && mode!="workshop" && state!=null)foreach(a in state.assemblies)if(a.id==selected)foreach(m in a.members)selectedIds[m.record] <- true;
    if(!ghost && mode=="visit" && state!=null)foreach(a in state.assemblies)if(a.id==navInside)foreach(m in a.members)openedIds[m.record] <- true;
    foreach(p in parts) {
        local focusLevel=mode=="visit"?navLevel:editLevel;
        local focused=cutaway || (p.instanceId in openedIds);
        if(focused && p.level>focusLevel)continue;
        local open=focused && p.level==focusLevel;
        local x=p.worldX,z=p.worldY,h=p.elevation+p.level*currentWorld().getFloorHeight();
        local mesh=villageCube;
        if(p.buildingId=="village.roof" && open)continue;
        local sx=0.96,sy=0.10,sz=0.96,y=h+0.05,yaw=0.0;
        local r=0.35,g=0.50,b=0.55;
        if(p.buildingId=="village.stairs") {
            local stair=villageStair(p);
            foreach(step in stair.boxes)villageBox(pool,count++,step.x,step.bottom+step.height*0.5,step.z,step.hw*2.0,step.height,step.hd*2.0,0.0,0.62,0.47,0.30,ghost?0.4:1.0);
            continue;
        }
        if(p.buildingId=="village.roof") {
            local turn=((p.rotationDegrees/90.0).tointeger()%2+2)%2;
            x=p.cellX+(turn==0?2.0:1.5);z=p.cellY+(turn==0?1.5:2.0);
            sx=4.16;sz=3.16;sy=1.0;y=h+0.5;
            yaw=p.rotationDegrees*0.01745329252;mesh=villageModels["roof-gable"];
            r=0.60;g=0.25;b=0.16;
        } else if(p.placementKind=="cell") {
            x=p.cellX+0.5;z=p.cellY+0.5;
            if(p.instanceId in selectedIds){r=0.60;g=0.52;b=0.30;}
        }
        else if(p.buildingId=="village.door") {
            local horizontal=p.edgeAxis=="horizontal";
            x=p.edgeX+(horizontal?0.5:0.0);z=p.edgeY+(horizontal?0.0:0.5);
            local height=open?0.65:currentWorld().getFloorHeight()-0.1;
            foreach(side in [-1.0,1.0])villageBox(pool,count++,x+(horizontal?side*0.46:0.0),h+0.1+height*0.5,z+(horizontal?0.0:side*0.46),horizontal?0.08:0.12,height,horizontal?0.12:0.08,0.0,0.40,0.25,0.13,ghost?0.4:1.0);
            if(!open)villageBox(pool,count++,x,h+0.1+height-0.05,z,horizontal?1.0:0.12,0.1,horizontal?0.12:1.0,0.0,0.40,0.25,0.13,ghost?0.4:1.0);
            if(villageDoorClosed(p))villageBox(pool,count++,x,h+0.1+(height-0.1)*0.5,z,horizontal?0.84:0.08,height-0.1,horizontal?0.08:0.84,0.0,0.55,0.30,0.14,ghost?0.4:1.0);
            continue;
        }
        else if(p.placementKind=="edge") {
            local horizontal=p.edgeAxis=="horizontal";
            x=p.edgeX+(horizontal?0.5:0.0);z=p.edgeY+(horizontal?0.0:0.5);
            sx=horizontal?1.0:0.12;sz=horizontal?0.12:1.0;sy=open?0.65:currentWorld().getFloorHeight()-0.1;
            y=h+0.1+sy*0.5;r=0.85;g=0.72;b=0.46;
            if(!open){mesh=villageModels["wall-window"];sx=1.0;sz=0.12;yaw=horizontal?0.0:1.57079632679;}
        } else if(p.placementKind=="corner") {
            x=p.cornerX;z=p.cornerY;sx=0.18;sz=0.18;sy=open?0.85:currentWorld().getFloorHeight();mesh=villageModels["post"];
            y=h+0.1+sy*0.5;r=0.95;g=0.50;b=0.35;
        } else {
            mesh=villageModels["chair"];sx=0.6;sz=0.4;sy=0.95;y=h+0.1+sy*0.5;
            // Free footprint angles rotate +X toward +Z; Renderable3D yaw is radians.
            yaw=-p.rotationDegrees*0.01745329252;
            local blue=("paint" in p.properties) && p.properties.paint=="blue";
            r=blue?0.25:0.55;g=blue?0.60:0.80;b=blue?0.95:0.39;
        }
        villageBox(pool,count,x,y,z,sx,sy,sz,yaw,ghost?0.25:r,ghost?0.95:g,ghost?0.80:b,ghost?0.4:1.0);
        pool[count].setMesh(mesh);count++;
    }
    villageHideAfter(pool,count);
}
function villageSetCamera() {
    local w=currentWorld().getWidth().tofloat(),d=currentWorld().getHeight().tofloat();
    local x=w*0.5,z=d*0.5,scale=mode=="workshop"?1.0:2.5;
    if((mode=="interior" || (mode=="visit" && selected==multiDemoId && multiDemoId!="")) && selected!="" && state!=null) {
        local ids={};foreach(a in state.assemblies)if(a.id==selected)foreach(m in a.members)ids[m.record] <- true;
        local n=0,sx=0.0,sz=0.0;
        foreach(p in state.instances)if(p.instanceId in ids){sx+=p.worldX;sz+=p.worldY;n++;}
        if(n>0){x=sx/n;z=sz/n;scale=1.2;}
    }
    // Shift the target toward screen-right to leave room for the fixed sidebar.
    villageCamera.setTarget(x+2.2*scale,mode=="visit"?navLevel*3.0:editLevel*3.0,z-2.2*scale);
    villageCamera.setEye(x+11.2*scale,11.0*scale+(mode=="visit"?navLevel*3.0:editLevel*3.0),z+9.8*scale);
}
function villageInitView() {
    villageCube=gfx.newMeshCube(1.0);villageLoadModels();villageCamera=eve.Camera3D();
    villageCamera.setFov(45.0);villageCamera.setUp(0.0,1.0,0.0);
    villageCamera.setAmbient(0.55,0.55,0.60);villageCamera.setActive(view3d);
    gfx.setDirectionalLight(-0.6,-1.0,-0.3,1.4,1.3,1.15);
    viewDirty=true;ghostDirty=true;
}
function villageSyncView() {
    if(villageCamera==null)return;
    villageCamera.setActive(view3d);
    if(!view3d) {
        villageHideAfter(villageSolids,0);villageHideAfter(villageGhosts,0);villageHideAfter(villageGrid,0);return;
    }
    if(viewDirty && state!=null) {
        villageSetCamera();villageProject(state.instances,villageSolids,false);
        local w=currentWorld().getWidth().tofloat(),d=currentWorld().getHeight().tofloat(),n=0;
        villageBox(villageGrid,n++,w/2,-0.08,d/2,w,0.10,d,0,0.12,0.17,0.18,1);
        for(local x=0;x<=w;x++)villageBox(villageGrid,n++,x,-0.02,d/2,0.018,0.01,d,0,0.25,0.32,0.33,1);
        for(local z=0;z<=d;z++)villageBox(villageGrid,n++,w/2,-0.02,z,w,0.01,0.018,0,0.25,0.32,0.33,1);
        villageHideAfter(villageGrid,n);viewDirty=false;
    }
    if(ghostDirty){villageProject(mode=="town"?preview:[],villageGhosts,true);ghostDirty=false;}
}
function villageMouseCell() {
    local mx=mouse.getX(),my=mouse.getY();
    if(mx<0 || my<0 || mx>=880 || my>=gfx.getHeight())return null;
    local x=0,y=0;
    if(view3d) {
        villageCamera.screenToRay(mx,my,gfx.getWidth().tofloat(),gfx.getHeight().tofloat());
        local dy=villageCamera.getScreenRayDirY();if(dy>=-0.0001)return null;
        local planeY=mode=="town"?0.0:editLevel*3.0;
        local t=(planeY-villageCamera.getScreenRayOriginY())/dy;if(t<0)return null;
        local wx=villageCamera.getScreenRayOriginX()+t*villageCamera.getScreenRayDirX();
        local wz=villageCamera.getScreenRayOriginZ()+t*villageCamera.getScreenRayDirZ();
        if(wx<0 || wz<0)return null;
        x=wx.tointeger();y=wz.tointeger();
    } else {
        if(mx<24 || my<50)return null;
        local cell=mode=="workshop"?80.0:32.0;
        x=((mx-24)/cell).tointeger();y=((my-50)/cell).tointeger();
    }
    if(x>=currentWorld().getWidth() || y>=currentWorld().getHeight())return null;
    return [x,y];
}
