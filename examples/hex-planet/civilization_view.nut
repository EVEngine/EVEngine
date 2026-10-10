// Orbital projection is presentation only; all cell picking is delegated to hexmap.
function civCameraBasis(st) {
    local y=st.yaw*DEG_TO_RAD; local p=st.pitch*DEG_TO_RAD;
    return { back=[cos(p)*sin(y),sin(p),cos(p)*cos(y)],
             right=[cos(y),0.0,-sin(y)], up=[-sin(p)*sin(y),cos(p),-sin(p)*cos(y)] };
}
function civDot(a,b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
function civProjectCell(st,cell) {
    local b=civCameraBasis(st); local n=hexmap.sphereDirection(cell);
    local radius=st.radius+hexmap.sphereElevation(cell)*hexmap.sphereElevationStep()+1.0;
    local distance=fitDistance(st)*st.zoom;
    if(civDot(n,b.back)<radius/distance) return null;
    local point=[n[0]*radius,n[1]*radius,n[2]*radius];
    local depth=distance-civDot(point,b.back);
    local scale=config.height*0.5/tan(FOV_DEGREES*0.5*DEG_TO_RAD)/depth;
    return [config.width*0.5+civDot(point,b.right)*scale,config.height*0.5-civDot(point,b.up)*scale];
}
function civFocus(st) {
    local n=hexmap.sphereDirection(civPosition(1));
    st.yaw=atan2(n[0],n[2])/DEG_TO_RAD;
    st.pitch=asin(n[1])/DEG_TO_RAD;
    st.spinning=false;
    applyCamera(st);
}
function civPick(st,x,y) {
    local b=civCameraBasis(st); local distance=fitDistance(st)*st.zoom;
    local spread=tan(FOV_DEGREES*0.5*DEG_TO_RAD);
    local sx=(2.0*x/config.width-1.0)*spread*config.width/config.height;
    local sy=(1.0-2.0*y/config.height)*spread;
    local ray=[];
    for(local i=0;i<3;i++) ray.push(-b.back[i]+b.right[i]*sx+b.up[i]*sy);
    local result=hexmap.spherePickCell(b.back[0]*distance,b.back[1]*distance,b.back[2]*distance,ray[0],ray[1],ray[2]);
    return result.ok ? result.value : -1;
}

// Presentation caches only: paths come from tactics; no mirrored simulation state.
civView <- {debug=false,mode="city",hover=-1,preview=null,revision="",font=null};
function civMapInput(x,y) {
    if(y<116 || y>config.height-152) return false;
    if(x<328) return false;
    if(x>=342 && x<=952 && y>=124 && y<=202) return false;
    if(civView.debug && x>config.width-350 && y<380) return false;
    return !ui.wantCaptureMouse();
}
function civScout() {
    civilization.selected=civPosition(1); civView.mode="unit"; civFocus(planet);
}
function civNextCity() {
    local cities=[];
    foreach(city in civCities()) if(city.settlement.owner==1) cities.push(city.settlement.cell);
    local next=0;
    foreach(i,cell in cities) if(cell==civilization.selected) next=(i+1)%cities.len();
    civilization.selected=cities[next]; civView.mode="city";
}
function civInput(st) {
    if(!ui.wantCaptureKeyboard()) {
        if(key_just_pressed("return","Return")) civEndTurn();
        if(key_just_pressed("f")) civFound(1);
        if(key_just_pressed("c")) civScout();
        if(key_just_pressed("1")) civButtonProject("granary");
        if(key_just_pressed("2")) civButtonProject("mine");
        if(key_just_pressed("3")) civButtonProject("library");
        if(key_just_pressed("4")) civButtonProject("wonder");
    }
    local x=mouse.getX(); local y=mouse.getY();
    local cell=civMapInput(x,y) ? civPick(st,x,y) : -1;
    local revision=cell+":"+civilization.turn+":"+civPosition(1)+":"+
        civValue(civilization.battle.unitResources(CIV_PLAYER)).movePoints+":"+civilization.outcome;
    if(revision!=civView.revision) {
        civView.revision=revision; civView.hover=cell; civView.preview=null;
        if(cell>=0 && civilization.outcome=="")
            civView.preview=civilization.battle.previewMove(CIV_PLAYER,cell,0,0);
    }
    local down=mouse.isDown(3);
    if(down && !st.civMouse && cell>=0) {
        local city=civCityAt(cell);
        if(city!=null && city.settlement.owner==1) {
            civilization.selected=cell; civView.mode="city";
        } else if(civMove(1,cell)) civView.mode="unit";
    }
    st.civMouse=down;
}
function civButtonProject(product) {
    local city=civCityAt(civilization.selected);
    if(civView.mode=="city" && city!=null && city.settlement.owner==1) civProject(city,product);
    else civilization.message="Select one of your cities with right click.";
}
function civStyleStatus(status) { if(status!="applied") throw "civilisation style: "+status; }
function civBegin(title) {
    ui.beginBuild(); ui.beginWindow(title,"root"); ui.setStyleScope("orbit.panel");
}
function civMount(name,x,y,w,h) {
    ui.end(); ui.mountBuildAs(name); ui.select(name);
    ui.setHostOverlay(true); ui.setHostPos(x,y,0.0,0.0); ui.setHostSize(w,h);
    ui.setHostVisible(true);
}
function civText(text,id,style="orbit.muted") {
    ui.text(text,id); ui.setItemStyleClass(style);
}
function civButton(text,id,tip) {
    ui.button(text,id); ui.setItemSize(256.0,32.0); ui.setItemTooltip(tip);
}
function civHud() {
    // Game HUD uses logical pixels at the fixed 1280x800 presentation size.
    ui.setScale(1.2);
    civStyleStatus(ui.defineStyleClass("orbit.panel",""));
    civStyleStatus(ui.setStyleClassColor("orbit.panel","background",0.055,0.105,0.145,0.96));
    civStyleStatus(ui.setStyleClassColor("orbit.panel","text",0.89,0.94,0.93,1.0));
    civStyleStatus(ui.setStyleClassColor("orbit.panel","border",0.20,0.35,0.39,0.8));
    civStyleStatus(ui.defineStyleClass("orbit.muted",""));
    civStyleStatus(ui.setStyleClassColor("orbit.muted","text",0.55,0.70,0.73,1.0));
    civStyleStatus(ui.defineStyleClass("orbit.gold",""));
    civStyleStatus(ui.setStyleClassColor("orbit.gold","text",0.95,0.79,0.43,1.0));
    civStyleStatus(ui.defineStyleClass("orbit.action",""));
    civStyleStatus(ui.setStyleClassColor("orbit.action","background",0.15,0.34,0.38,1.0));
    civStyleStatus(ui.setStyleClassColor("orbit.action","accent",0.25,0.66,0.61,1.0));
    civStyleStatus(ui.setStyleClassColor("orbit.action","text",0.94,1.0,0.95,1.0));

    civBegin("Empire");
    ui.beginRow("resources",22.0);
    civText("HEX / CIVILISATION","brand","orbit.gold");
    ui.text("","gold"); ui.text("","food"); ui.text("","science");
    ui.button("World / debug","debug"); ui.end();
    ui.separator("rule"); ui.text("","objective");
    civMount("civ-resources",12.0,12.0,config.width-24.0,100.0);
    ui.onClick("debug",function(){civView.debug=!civView.debug;});

    civBegin("Selection");
    civText("YOUR CIVILISATION","faction","orbit.gold");
    ui.text("","selection"); civText("","city");
    ui.button("Cities","cities"); ui.sameLine("nav"); ui.button("Expedition [C]","focus");
    ui.separator("division");
    ui.beginGroup("city-content");
    civText("","yield"); civText("","growth");
    ui.text("","project"); ui.progress(0.0,"progress"," "); ui.setItemSize(256.0,12.0);
    local index=1;
    foreach(product in ["granary","mine","library","wonder"]) {
        local d=civProjects[product];
        civButton(index+"  "+d.label,product,d.effect);
        civText(d.effect,product+"-effect");
        index++;
    }
    ui.end();
    ui.beginGroup("unit-content");
    ui.text("","movement");
    ui.textWrapped("Hover land to preview your route. Right click to move.",256.0,"unit-help");
    ui.separator("unit-rule");
    civButton("Found city [F] - 30 gold","found","Build at least 3 tiles from every existing city.");
    ui.textWrapped("",256.0,"found-reason");
    ui.end();
    civMount("civilization",12.0,128.0,304.0,550.0);
    ui.onClick("cities",function(){civNextCity();});
    ui.onClick("focus",function(){civScout();});
    ui.onClick("granary",function(){civButtonProject("granary");});
    ui.onClick("mine",function(){civButtonProject("mine");});
    ui.onClick("library",function(){civButtonProject("library");});
    ui.onClick("wonder",function(){civButtonProject("wonder");});
    ui.onClick("found",function(){civFound(1);});

    civBegin("Council");
    civText("TURN COUNCIL","caption","orbit.gold"); ui.text("","turn");
    ui.button("End turn [Enter]","endturn"); ui.setItemSize(264.0,42.0); ui.setItemStyleClass("orbit.action");
    civMount("civ-turn",config.width-316.0,config.height-152.0,304.0,140.0);
    ui.onClick("endturn",function(){civEndTurn();});

    civBegin("Dispatches");
    civText("DISPATCHES","heading","orbit.gold");
    for(local i=0;i<3;i++) ui.text("","event"+i);
    civMount("civ-events",342.0,config.height-152.0,610.0,140.0);

    civBegin("Route");
    ui.textWrapped("",560.0,"message");
    civMount("civ-hint",342.0,124.0,610.0,78.0);

    civBegin("World details");
    ui.text("","planet"); ui.text("LMB orbit | Wheel zoom","camera");
    ui.text("R new world | F5 capture","controls");
    ui.text("[ ] detail | - + land","terrain");
    civMount("hud",config.width-342.0,212.0,330.0,154.0);
    ui.setHostVisible(false);
    local data=eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf",16);
    civView.font=gfx.newFont(data," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789[]()/|-:+',.!?");
}
function civRefresh() {
    local c=civilization; local gold=0; local food=0; local science=0;
    foreach(city in eve.view(Colony)) if(city.settlement.owner==1) {
        local income=civYield(city); gold+=income.gold; food+=income.food; science+=income.science;
    }
    ui.select("civ-resources");
    ui.setText("gold","Gold "+civBalance(1,"gold")+"  (+"+gold+")");
    ui.setText("food","Food "+civBalance(1,"food")+"  (+"+food+")");
    ui.setText("science","Science "+civBalance(1,"science")+"  (+"+science+")");
    ui.setText("objective","BEACON VICTORY    Cities "+civCount(1)+" / 3    |    "+
        (c.technology[1] ? "Writing discovered" : "Writing "+civBalance(1,"science")+" / 24")+
        "    |    Complete the Beacon");
    ui.select("civilization");
    local city=civCityAt(c.selected);
    local own=city!=null && city.settlement.owner==1 && civView.mode=="city";
    ui.setVisible("city-content",own); ui.setVisible("unit-content",!own);
    ui.setHostSize(304.0,own ? 550.0 : 316.0);
    ui.setText("selection",own ? civCityName(c.selected) : "Pathfinder expedition");
    ui.setText("city",own ? "Population "+city.settlement.population+"  |  Your city" : "Your expedition  |  Tile "+civPosition(1));
    if(own) {
        local income=civYield(city); local s=city.settlement;
        ui.setText("yield","Per round: "+income.gold+"g  "+income.food+" food  "+income.science+" science");
        ui.setText("growth",s.population<5 ? "Growth uses "+(8+s.population*4)+" shared food" : "Population at maximum");
        local project="Choose a city project"; local progress=0.0;
        if(city.work.task!="") {
            local task=city.work.queue.find(city.work.task);
            local turns=ceil((task.getDuration()-task.getProgress())/income.work).tointeger();
            project=civProjects[task.getProduct()].label+" - "+turns+" rounds left";
            progress=task.getProgress()/task.getDuration();
        }
        ui.setText("project",project); ui.setValue("progress",progress);
        local index=1;
        foreach(product in ["granary","mine","library","wonder"]) {
            local d=civProjects[product]; local reason=civProjectReason(city,product);
            local turns=ceil(d.work/income.work).tointeger();
            ui.setEnabled(product,reason=="");
            ui.setText(product,index+" "+d.label+"  "+d.gold+"g / "+turns+"r");
            ui.setText(product+"-effect",reason=="" ? d.effect : reason);
            index++;
        }
    }
    ui.setText("movement","Movement  "+civValue(c.battle.unitResources(CIV_PLAYER)).movePoints+" / 4");
    local reason=civFoundReason(1);
    ui.setEnabled("found",reason=="");
    ui.setText("found-reason",reason=="" ? "Ready to settle here." : reason);
    ui.select("civ-turn");
    ui.setText("turn","Round "+c.turn+"   |   Rival cities "+civCount(2));
    ui.setEnabled("endturn",c.outcome=="");
    ui.select("civ-events");
    for(local i=0;i<3;i++) ui.setText("event"+i,i<c.notices.len() ? c.notices[c.notices.len()-1-i] :
        (i==0 ? "Your people look to the stars." : ""));
    local message=c.outcome!="" ? c.outcome : c.message;
    if(civView.hover>=0 && c.outcome=="") {
        local target=civCityAt(civView.hover);
        if(target!=null && target.settlement.owner==1) message="Right click: select "+civCityName(civView.hover);
        else if(civView.preview!=null && civView.preview.ok)
            message="Move here: "+civView.preview.value.cost+" points | "+civView.preview.value.remainingMovePoints+" left. Right click to confirm.";
        else message=hexmap.sphereIsUnderwater(civView.hover) ? "Ocean: this expedition travels on land." : "Not reachable this turn. End turn to restore movement.";
    }
    ui.select("civ-hint"); ui.setText("message",message);
    ui.select("hud"); ui.setHostVisible(civView.debug);
    ui.setText("planet",describePlanet(planet));
}
function civMarker(st,cell,size,r,g,b) {
    local p=civProjectCell(st,cell);
    if(p==null) return;
    gfx.drawSolidRect(p[0]-size-1,p[1]-size-1,size*2+2,size*2+2,0.02,0.03,0.06,1.0);
    gfx.drawSolidRect(p[0]-size,p[1]-size,size*2,size*2,r,g,b,1.0);
}
function civLabel(st,cell,text,offset,r,g,b) {
    local p=civProjectCell(st,cell);
    if(p==null || p[0]<330 || p[1]<206 || p[1]>config.height-164) return;
    gfx.drawSolidRect(p[0]+10,p[1]+offset-2,text.len()*9.5+12,24.0,0.035,0.07,0.10,0.93);
    gfx.drawText(civView.font,text,p[0]+16,p[1]+offset,r,g,b,1.0,1.0);
}
function civRender(st) {
    if(civilization==null) return;
    foreach(entry in civilization.reachable) civMarker(st,entry.cell.x,1.5,0.28,0.75,0.63);
    if(civView.preview!=null && civView.preview.ok) {
        local previous=null;
        foreach(cell in civView.preview.value.path) {
            local point=civProjectCell(st,cell.x);
            if(previous!=null && point!=null) {
                for(local t=0.0;t<=1.0;t+=0.12)
                    gfx.drawSolidRect(previous[0]+(point[0]-previous[0])*t-2,
                        previous[1]+(point[1]-previous[1])*t-2,4.0,4.0,0.95,0.79,0.43,1.0);
            }
            previous=point;
        }
    }
    foreach(city in eve.view(Colony)) {
        local s=city.settlement;
        if(s.owner==1 || s.cell in civilization.explored) {
            civMarker(st,s.cell,7.0,s.owner==1 ? 0.2 : 1.0,s.owner==1 ? 0.75 : 0.4,0.85);
            civLabel(st,s.cell,(s.owner==1 ? "" : "Rival / ")+civCityName(s.cell)+"  ["+s.population+"]",12,0.70,0.94,0.92);
        }
    }
    civMarker(st,civilization.selected,9.0,1.0,0.83,0.35);
    civMarker(st,civPosition(1),4.0,0.95,1.0,1.0);
    civLabel(st,civPosition(1),"Pathfinder  /  "+civValue(civilization.battle.unitResources(CIV_PLAYER)).movePoints+" MP",-32,0.95,0.79,0.43);
}
