persist arenaGun = [];
persist arenaTargetTrim = [];

function arenaDress() {
    // All dressing lies on the surfaces of the existing collision shell.
    for(local x=-7;x<=7;x+=2) {
        makeBox(x,0.012,0,0.035,0.02,19,0.10,0.15,0.17);
        makeBox(x,2.25,-9.76,0.12,4.5,0.08,0.11,0.20,0.24);
        makeBox(x,3.85,-9.71,0.75,0.10,0.10,0.26,0.71,0.74).setReceiveLight(false);
    }
    for(local z=-9;z<=9;z+=2) {
        makeBox(0,0.015,z,15.5,0.02,0.035,0.10,0.15,0.17);
        for(local side=-1;side<=1;side+=2) {
            makeBox(side*7.76,2.2,z,0.12,4.4,0.18,0.11,0.18,0.22);
            makeBox(side*7.72,0.45,z,0.13,0.14,1.6,0.71,0.47,0.16);
        }
    }
    makeBox(0,0.025,-2.0,12.5,0.025,0.11,0.68,0.47,0.18);
    for(local x=-5;x<=5;x+=2)makeBox(x,0.027,1,0.65,0.03,0.08,0.62,0.66,0.61);
    foreach(t in targets) {
        local pieces=[];
        pieces.push(makeBox(t.x,1.15,t.z+0.365,0.53,0.09,0.02,0.94,0.78,0.43));
        pieces.push(makeBox(t.x,0.8,t.z+0.365,0.08,0.42,0.02,0.18,0.21,0.23));
        pieces.push(makeBox(t.x,1.6,t.z+0.365,0.22,0.17,0.02,0.17,0.22,0.23));
        arenaTargetTrim.push({target=t,parts=pieces});
    }
    // A simple receiver, barrel, magazine and optic; follows the actual camera.
    foreach(p in [[0.24,-0.24,0.52,0.16,0.13,0.42], [0.24,-0.20,0.88,0.055,0.055,0.32],
                  [0.24,-0.11,0.56,0.075,0.085,0.17], [0.24,-0.32,0.39,0.075,0.20,0.10],
                  [0.24,-0.36,0.60,0.09,0.20,0.13], [0.24,-0.22,0.24,0.13,0.15,0.20],
                  [0.24,-0.23,0.73,0.14,0.16,0.20]]) {
        local e=makeBox(0,0,0,p[3],p[4],p[5],0.10,0.14,0.17);
        e.setCastShadow(false);e.setReceiveShadow(false);e.setReceiveLight(false);
        local i=arenaGun.len();
        if(i==6)e.setTint(0.30,0.23,0.14,1.0);
        else if(i==1 || i==2)e.setTint(0.035,0.045,0.05,1.0);
        else if(i==4)e.setTint(0.16,0.19,0.11,1.0);
        arenaGun.push({e=e,x=p[0],y=p[1],z=p[2]});
    }
}
function arenaSyncViewmodel() {
    local eye=eyePos();local yaw=lookYaw*0.01745329252,pitch=lookPitch*0.01745329252;
    local sy=sin(yaw),cy=cos(yaw),sp=sin(pitch),cp=cos(pitch);
    local ads=rifle!=null && rifle.isAiming();
    foreach(p in arenaGun) {
        local x=ads?p.x*0.35:p.x,y=ads?p.y*0.70:p.y;
        local z=p.z+0.20-(hitFlash>0?0.025:0.0);
        p.e.setPosition(eye[0]-cy*x-sy*sp*y+sy*cp*z,
            eye[1]+cp*y+sp*z,eye[2]+sy*x-cy*sp*y+cy*cp*z);
        p.e.setRotation(yaw,-pitch,0.0);
    }
    foreach(trim in arenaTargetTrim)foreach(e in trim.parts)e.setVisible(trim.target.view.health>0.0);
}
