// Reinforced medical case: pale shell, gasket, corner guards and raised field cross.
export function buildMedicalBox(m,a) {
    local shell=[0.62,0.65,0.58],lid=[0.48,0.53,0.46],guard=[0.20,0.27,0.23],metal=[0.40,0.44,0.40];
    local seal=[0.05,0.075,0.06],red=[0.59,0.08,0.055],white=[0.82,0.83,0.73];
    m.hullLoft([[-0.38,0.48,0.08,0.68],[-0.30,0.60,0.03,0.72],[0.30,0.60,0.03,0.72],[0.38,0.48,0.08,0.68]],shell,"body",0.20);
    m.armorBox(0,0.71,0,1.22,0.04,0.79,seal);
    m.armorBox(0,0.77,0,1.25,0.09,0.81,lid);
    m.armorBox(0,0.824,0,1.11,0.025,0.67,shell);
    foreach(s in [-1,1]) {
        foreach(x in [-0.52,0.52]) {
            foreach(y in [0.12,0.66]) {
                m.armorBox(x,y,s*0.37,0.15,0.20,0.10,guard);
                m.roundTube(x,y,s*0.427,0.025,0.012,metal);
            }
        }
        m.armorBox(s*0.43,0.64,0.43,0.12,0.22,0.035,metal);
        m.armorBox(s*0.43,0.68,0.453,0.065,0.15,0.02,guard);
        m.roundTube(s*0.43,0.57,0.459,0.023,0.02,metal);
        m.roundTube(s*0.4,0.76,-0.435,0.035,0.22,guard,"body",1.5708);
        m.armorBox(s*0.62,0.43,0,0.035,0.23,0.31,guard);
        m.braceBeam([s*0.65,0.51,-0.12],[s*0.69,0.35,-0.08],0.04,metal);
        m.braceBeam([s*0.69,0.35,-0.08],[s*0.69,0.35,0.08],0.04,metal);
        m.braceBeam([s*0.69,0.35,0.08],[s*0.65,0.51,0.12],0.04,metal);
        m.armorBox(s*0.47,0.045,0,0.18,0.09,0.70,guard);
    }
    m.armorBox(0,0.38,0.392,0.52,0.42,0.022,red);
    m.armorBox(0,0.38,0.407,0.33,0.095,0.014,white);
    m.armorBox(0,0.38,0.408,0.095,0.31,0.014,white);
    m.armorBox(0,0.84,-0.08,0.42,0.012,0.36,red);
    m.armorBox(0,0.851,-0.08,0.29,0.012,0.085,white);
    m.armorBox(0,0.852,-0.08,0.085,0.012,0.27,white);
    m.stencilGlyphs("MED",0,0.38,-0.395,0.026,guard,3.14159265);
    m.armorBox(0,0.22,-0.394,0.51,0.018,0.008,red);
    m.armorBox(0,0.84,0.24,0.53,0.02,0.10,guard);
    m.braceBeam([-0.22,0.88,0.24],[-0.17,1.0,0.24],0.045,guard);
    m.braceBeam([-0.17,1.0,0.24],[0.17,1.0,0.24],0.045,guard);
    m.braceBeam([0.17,1.0,0.24],[0.22,0.88,0.24],0.045,guard);
}
