// Three staggered courses, pinched cloth ends and a sewn edge per bag.
export function buildSandbags(m,a) {
    local cloth=[[0.50,0.42,0.27],[0.56,0.46,0.30],[0.46,0.39,0.26]];
    for(local row=0;row<3;row++) for(local i=0;i<5;i++) {
        local x=(i-2)*0.79+(row%2)*0.31,y=0.15+row*0.28;
        local yaw=((i+row)%3-1)*0.035;
        m.filledBag(x,y,((i+row)%2)*0.015,0.92,0.29,0.57,cloth[(i+row)%3],yaw);
    }
    m.filledBag(-2.16,0.14,0.45,0.90,0.28,0.55,cloth[1],-0.34);
}
