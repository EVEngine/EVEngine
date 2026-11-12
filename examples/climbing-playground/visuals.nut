// Presentation follows authoritative feet/phase; no second movement simulation.
persist climbActor = [];
persist climbHudReady = false;
persist climbOverview = false;

function climbBuildPresentation() {
    local colors = [[0.13,0.58,0.66],[0.22,0.66,0.43],[0.82,0.56,0.19],
                    [0.50,0.36,0.68],[0.73,0.24,0.22],[0.12,0.65,0.66]];
    for(local i=0;i<6;i++) {
        local x=-4.0+i*2.0;
        makeVisual(x,0.014,2.0,1.5,0.025,5.4,0.10,0.15,0.19);
        makeVisual(x,0.035,-0.85,1.4,0.025,0.12,colors[i][0],colors[i][1],colors[i][2]);
        for(local z=-1.0;z<1.7;z+=0.55)
            makeVisual(x,0.04,z,0.07,0.02,0.24,0.55,0.64,0.63);
        local heights=[0.6,1.0,1.4,1.2,1.0,1.0];
        if(i!=5) {
            makeVisual(x,heights[i]+0.012,2.0,1.4,0.035,0.72,0.78,0.85,0.78);
            for(local h=0.18;h<heights[i];h+=0.3)
                makeVisual(x,h,1.665,0.8,0.055,0.03,0.13,0.19,0.21);
        }
    }
    for(local x=-6;x<=8;x+=2)
        makeVisual(x,0.01,2.4,0.025,0.02,6.6,0.25,0.31,0.32);
    for(local z=-0.5;z<6;z+=1.0)
        makeVisual(1.0,0.012,z,14.0,0.025,0.025,0.25,0.31,0.32);
    player.setScale(0.46,0.68,0.32);
    player.setTint(0.12,0.35,0.45,1.0);
    foreach(spec in [[0.0,1.48,0.0,0.34,0.32,0.32,0.76,0.56,0.39],
                     [0.0,1.68,0.0,0.39,0.13,0.36,0.87,0.65,0.24],
                     [-0.34,1.04,0.0,0.16,0.58,0.18,0.16,0.43,0.49],
                     [0.34,1.04,0.0,0.16,0.58,0.18,0.16,0.43,0.49],
                     [-0.14,0.36,0.0,0.20,0.66,0.22,0.12,0.19,0.23],
                     [0.14,0.36,0.0,0.20,0.66,0.22,0.12,0.19,0.23],
                     [-0.14,0.09,0.08,0.24,0.16,0.36,0.08,0.11,0.13],
                     [0.14,0.09,0.08,0.24,0.16,0.36,0.08,0.11,0.13]]) {
        local e=makeVisual(0,0,0,spec[3],spec[4],spec[5],spec[6],spec[7],spec[8]);
        climbActor.push({e=e,x=spec[0],y=spec[1],z=spec[2]});
    }
    ui.beginBuild();ui.beginWindow("CLIMBING / TRAINING YARD","root");
    ui.text("01 LOW VAULT   02 HIGH VAULT   03 MANTLE","title");
    ui.text("04 LEDGE GRAB   05 BLOCKED   06 MOVING","lanes");
    ui.text("1-6 lane | Space start | R reset | O overview","help");
    ui.textWrapped("",450.0,"state");ui.end();ui.mountBuildAs("climb-hud");ui.select("climb-hud");
    ui.setHostOverlay(true);ui.setHostPos(18.0,16.0,0.0,0.0);ui.setHostSize(490.0,200.0);
    climbHudReady=true;
}

function climbSyncPresentation() {
    if(edgePressed("o"))climbOverview=!climbOverview;
    if(climbOverview){camera.setEye(10.0,5.7,10.3);camera.setTarget(1.0,0.9,2.2);}
    else {local x=-4.0+(lane-1)*2.0;camera.setEye(x+4.1,3.6,7.6);camera.setTarget(x+0.35,0.95,1.1);}
    foreach(i,part in climbActor) {
        local raise=active && (i==2 || i==3) ? 0.25 : 0.0;
        part.e.setPosition(playerFeet[0]+part.x,playerFeet[1]+part.y+raise,playerFeet[2]+part.z);
        if(i==2 || i==3) part.e.setRotation(0.0,active ? -0.9 : 0.08,0.0);
    }
    if(climbHudReady) {
        ui.select("climb-hud");
        ui.setText("state","LANE "+lane+"  |  "+lastPhase+"  |  "+lastCode);
    }
}
