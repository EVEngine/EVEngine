// Visual proxy only. The visitor's position is owned by navigation.nut.
function navDraw3D() {
    if(!view3d || mode=="workshop" || navGraph==null){villageHideAfter(navBody,0);villageHideAfter(navTrail,0);return;}
    local groundY=navY;
    local stride=(navRoute.len()>0 || navManualTicks>0 || navManualActive)?sin(navDistance*12.0)*0.05:0.0;
    local c=cos(navYaw),s=sin(navYaw);
    local pieces=[
        [0.0,0.77,0.0,0.18,0.55,0.12,0.95,0.35,0.15],
        [0.0,1.17,0.0,0.20,0.22,0.20,0.95,0.80,0.58],
        [-0.06,0.25+stride,0.0,0.06,0.48,0.08,0.18,0.26,0.40],
        [0.06,0.25-stride,0.0,0.06,0.48,0.08,0.18,0.26,0.40],
        [-0.11,0.77,0.0,0.04,0.44,0.06,0.95,0.65,0.36],
        [0.11,0.77,0.0,0.04,0.44,0.06,0.95,0.65,0.36]
    ];
    foreach(i,p in pieces)villageBox(navBody,i,navX+p[0]*c+p[2]*s,groundY+p[1],navZ-p[0]*s+p[2]*c,p[3],p[4],p[5],navYaw,p[6],p[7],p[8],1.0);
    local n=0;
    for(local i=navNext;i<navRoute.len();i+=4) {
        local p=navRoute[i];villageBox(navTrail,n++,p[0],p[2]+0.03,p[1],0.07,0.025,0.07,0.0,0.10,0.90,0.85,1.0);
    }
    villageHideAfter(navTrail,n);
}
function navDrawPlan() {
    if(mode=="workshop" || navGraph==null)return;
    for(local i=navNext;i<navRoute.len();i+=4)gfx.drawSolidRect(24+navRoute[i][0]*32-1.5,50+navRoute[i][1]*32-1.5,3.0,3.0,0.1,0.9,0.85,1.0);
    gfx.drawSolidRect(24+navX*32-5,50+navZ*32-5,10.0,10.0,1.0,0.35,0.15,1.0);
}
