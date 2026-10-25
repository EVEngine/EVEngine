// Straight-wing close-support jet: high twin turbofans, twin fins, seven-barrel nose gun.
export function buildAttackAircraft(m,a) {
    local paint=[0.36,0.40,0.37],edge=[0.49,0.52,0.45],metal=[0.20,0.25,0.25],black=[0.04,0.055,0.055],glass=[0.10,0.27,0.34];
    a.air=true;
    m.hullLoft([[-4.6,0.25,-0.20,0.25],[-3.2,0.54,-0.40,0.45],[-0.8,0.67,-0.55,0.55],[1.7,0.58,-0.50,0.48],[3.7,0.44,-0.34,0.25],[4.5,0.23,-0.25,0.05]],paint);
    m.hullLoft([[1.0,0.32,0.40,0.50],[1.5,0.41,0.40,1.05],[2.6,0.37,0.33,0.98],[3.15,0.27,0.25,0.42]],glass,"body",0.10);
    foreach(s in [-1,1]) {
        m.wingPanel([[s*0.5,0.8],[s*5.9,0.5],[s*6.1,-0.65],[s*0.55,-1.1]],-0.20,0.15,paint);
        m.wingPanel([[s*0.3,-3.4],[s*2.4,-3.5],[s*2.5,-4.3],[s*0.3,-4.5]],0.25,0.10,paint);
        m.finPlate([[-4.5,0.20],[-4.1,1.5],[-3.45,1.6],[-3.25,0.25]],s*1.65,0.13,paint);
        m.profileTube(s*1.04,0.66,-2.65,[[-1.1,0.34],[-0.95,0.46],[0.85,0.58],[1.20,0.58]],paint);
        m.roundTube(s*1.04,0.66,-1.44,0.45,0.018,black);
        local fan=s<0?"prop0":"prop1";
        m.setJointPivot(fan,s*1.04,0.66,-1.405);
        m.roundTube(0,0,0,0.13,0.025,metal,fan);
        for(local i=0;i<12;i++) {
            local t=i*0.5236;
            m.braceBeam([cos(t)*0.15,sin(t)*0.15,0],
                [cos(t+0.2)*0.41,sin(t+0.2)*0.41,0],0.025,metal,fan);
        }
        m.roundTube(s*1.04,0.66,-3.765,0.27,0.018,black);
        m.braceBeam([s*0.5,0.38,-2.2],[s*1.0,0.58,-2.2],0.17,metal);
        m.braceBeam([s*0.4,0.53,1.5],[s*0.29,0.43,3.1],0.045,metal);
        m.braceBeam([s*0.39,0.54,1.5],[s*0.34,0.98,1.8],0.045,metal);
        m.braceBeam([s*0.5,-0.11,-0.82],[s*5.6,-0.11,-0.60],0.018,metal);
        for(local i=0;i<3;i++) {
            local x=s*(2.2+i*1.15);
            m.armorBox(x,-0.45,-0.05,0.13,0.42,0.85,metal);
            m.profileTube(x,-0.79,0.1,[[-1.0,0.09],[-0.85,0.17],[0.7,0.17],[1.0,0.025]],edge);
            m.wingPanel([[x-0.35,-0.68],[x+0.35,-0.68],[x+0.35,-1.0],[x-0.35,-1.0]],-0.79,0.035,metal);
        }
        m.armorBox(s*5.9,-0.11,0.12,0.18,0.06,0.22,s<0?[0.55,0.06,0.04]:[0.04,0.4,0.15]);
        m.wingPanel([[s*0.75,-0.15],[s*1.5,-0.18],[s*1.5,-0.8],[s*0.75,-0.9]],-0.11,0.015,metal);
    }
    m.profileTube(0,-0.23,4.25,[[0,0.16],[0.5,0.16]],metal);
    for(local i=0;i<7;i++) { local t=i*0.8976;
        m.roundTube(cos(t)*0.095,-0.23+sin(t)*0.095,4.76,0.033,0.02,black); }
    m.armorBox(0,0.64,-1.0,0.05,0.35,0.2,metal);
    m.armorBox(0,-0.6,1.8,0.28,0.22,0.35,metal);
}
