// Low elongated steel ammunition can: stamped ribs, lid gasket, catches and stencils.
export function buildAmmunitionBox(m,a) {
    local green=[0.25,0.31,0.18],edge=[0.37,0.40,0.24],metal=[0.18,0.22,0.18],seal=[0.045,0.06,0.04],ink=[0.70,0.60,0.26];
    m.hullLoft([[-0.34,0.64,0.08,0.63],[-0.28,0.72,0.04,0.67],[0.28,0.72,0.04,0.67],[0.34,0.64,0.08,0.63]],green);
    m.armorBox(0,0.66,0,1.47,0.045,0.71,seal);
    m.armorBox(0,0.715,0,1.51,0.09,0.75,edge);
    m.armorBox(0,0.765,0,1.36,0.025,0.60,green);
    foreach(s in [-1,1]) {
        for(local i=0;i<5;i++) m.armorBox(-0.56+i*0.28,0.31,s*0.347,0.045,0.39,0.018,edge);
        m.armorBox(s*0.43,0.55,0.365,0.15,0.22,0.035,metal);
        m.armorBox(s*0.43,0.59,0.392,0.07,0.18,0.025,edge);
        m.roundTube(s*0.43,0.49,0.411,0.027,0.015,[0.44,0.44,0.30]);
        m.roundTube(s*0.43,0.70,-0.38,0.034,0.23,metal,"body",1.5708);
        m.armorBox(s*0.735,0.39,0,0.035,0.19,0.37,seal);
        m.braceBeam([s*0.76,0.45,-0.15],[s*0.81,0.32,-0.11],0.035,metal);
        m.braceBeam([s*0.81,0.32,-0.11],[s*0.81,0.32,0.11],0.035,metal);
        m.braceBeam([s*0.81,0.32,0.11],[s*0.76,0.45,0.15],0.035,metal);
        m.armorBox(s*0.60,0.08,0,0.08,0.13,0.66,metal);
    }
    m.armorBox(0,0.787,0,0.44,0.014,0.16,seal);
    m.braceBeam([-0.18,0.82,0],[-0.15,0.90,0],0.033,metal);
    m.braceBeam([-0.15,0.90,0],[0.15,0.90,0],0.033,metal);
    m.braceBeam([0.15,0.90,0],[0.18,0.82,0],0.033,metal);
    m.armorBox(0,0.36,0.363,0.62,0.24,0.015,green);
    m.stencilGlyphs("AMMO",0,0.38,0.374,0.022,ink);
    m.armorBox(0,0.225,0.363,0.49,0.017,0.008,ink);
    foreach(x in [-0.17,0.0,0.17]) m.armorBox(x,0.785,-0.22,0.09,0.008,0.07,ink);
}
