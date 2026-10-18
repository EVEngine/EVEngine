// Tandem attack helicopter: stepped cockpit, turbine pods, stub wings, wheeled gear.
export function buildHelicopter(m,a) {
    local paint=[0.25,0.32,0.27],edge=[0.38,0.43,0.32],metal=[0.14,0.18,0.18],black=[0.035,0.045,0.045],glass=[0.10,0.29,0.36],tip=[0.66,0.57,0.27];
    a.air=true;
    m.setJointPivot("rotor",0,1.70,-0.40); m.setJointPivot("tailrotor",0.3,1.05,-6.3);
    m.setJointPivot("turret",0,-0.72,2.2); m.setJointPivot("barrel",0,-0.12,0.12);
    m.hullLoft([[-2.6,0.45,-0.5,0.55],[-1.5,0.88,-0.72,0.85],[0.4,0.82,-0.70,0.80],[2.5,0.53,-0.45,0.38],[3.2,0.24,-0.28,0.10]],paint, "body",0.22);
    m.hullLoft([[-6.6,0.16,0.75,1.05],[-5.4,0.23,0.35,0.9],[-2.0,0.45,-0.3,0.55]],paint);
    m.hullLoft([[-0.80,0.48,0.55,0.90],[0.0,0.58,0.55,1.25],[0.9,0.56,0.40,1.05],[1.3,0.47,0.34,0.70],[2.2,0.39,0.22,0.69],[2.72,0.29,0.10,0.29]],glass,"body",0.10);
    foreach(s in [-1,1]) {
        m.braceBeam([s*0.53,0.56,0],[s*0.53,1.18,0],0.065,metal);
        m.braceBeam([s*0.49,0.42,1.2],[s*0.48,0.85,1.18],0.065,metal);
        m.braceBeam([s*0.50,1.20,0],[s*0.44,0.80,1.2],0.05,metal);
        m.braceBeam([s*0.44,0.80,1.2],[s*0.30,0.30,2.65],0.05,metal);
        m.profileTube(s*0.67,0.65,-1.1,[[-1.25,0.25],[-1,0.35],[0.35,0.38],[0.7,0.30]],metal);
        m.roundTube(s*0.67,0.65,-0.38,0.25,0.02,black);
        m.roundTube(s*0.67,0.65,-2.36,0.19,0.03,black);
        m.wingPanel([[s*0.55,0.2],[s*2.8,-0.3],[s*2.9,-1.0],[s*0.6,-0.9]],-0.12,0.12,paint);
        m.profileTube(s*2.3,-0.48,-0.2,[[-0.8,0.29],[0.65,0.29],[0.8,0.26]],metal);
        for(local i=0;i<7;i++) { local t=i*0.8976;
            m.roundTube(s*2.3+cos(t)*0.17,-0.48+sin(t)*0.17,0.61,0.052,0.015,black); }
        m.profileTube(s*1.35,-0.43,-0.1,[[-1.0,0.09],[0.75,0.09],[1.0,0.018]],edge);
        m.braceBeam([s*0.67,-0.4,-0.9],[s*1.25,-1.25,-1.1],0.09,metal);
        m.roundTube(s*1.28,-1.25,-1.1,0.27,0.19,black,"body",1.5708);
        m.roundTube(s*1.39,-1.25,-1.1,0.13,0.03,edge,"body",1.5708);
        for(local i=0;i<5;i++) m.armorBox(s*0.89,0.28,-1.8+i*0.18,0.025,0.10,0.10,black);
    }
    m.finPlate([[-6.6,0.7],[-6.5,2.0],[-5.9,2.25],[-5.8,0.7]],0,0.12,paint);
    m.wingPanel([[-1.5,-5.7],[1.5,-5.7],[1.4,-6.2],[-1.4,-6.2]],0.75,0.09,paint);
    m.roundTube(0,1.24,-0.4,0.11,0.9,metal,"body",0,1.5708);
    m.roundTube(0,0,0,0.31,0.18,metal,"rotor",0,1.5708);
    for(local i=0;i<4;i++) {
        local t=i*1.5708,points=[],tips=[];
        foreach(p in [[0.25,-0.10],[5.6,-0.18],[5.85,0.10],[0.7,0.28]])
            points.push([p[0]*cos(t)+p[1]*sin(t),-p[0]*sin(t)+p[1]*cos(t)]);
        foreach(p in [[5.4,-0.16],[5.6,-0.18],[5.85,0.10],[5.5,0.11]])
            tips.push([p[0]*cos(t)+p[1]*sin(t),-p[0]*sin(t)+p[1]*cos(t)]);
        m.wingPanel(points,0,0.045,metal,"rotor"); m.wingPanel(tips,0.028,0.01,tip,"rotor");
        m.armorBox(0,sin(t)*0.65,cos(t)*0.65,0.05,0.14,1.3,metal,"tailrotor",0,-t);
    }
    m.roundTube(0,0,0,0.13,0.28,metal,"tailrotor",1.5708);
    m.braceBeam([0,-0.5,2.1],[0,-1.35,2.2],0.10,metal);
    m.roundTube(0,-1.38,2.2,0.22,0.20,black,"body",1.5708);
    m.roundTube(0,0,0,0.25,0.30,metal,"turret",0,1.5708);
    m.profileTube(0,0,0,[[0,0.065],[0.65,0.055],[0.8,0.07]],metal,"barrel");
    m.roundTube(0,0,0.807,0.035,0.01,black,"barrel");
}
