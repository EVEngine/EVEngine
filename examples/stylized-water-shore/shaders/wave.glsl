// Every authored temporal frequency is an integer multiple of .01 radians/s.
// Wrap before adding spatial coordinates, preserving small spatial differences
// in long-running vertex/fragment normal calculations. All animated noise uses
// periodic drift too, so the wrap is continuous.
float shoreWrappedTime(float time) { return mod(time,628.3185307); }
// Shared incoming wave train: geometry, normals and foam use the same phase.
float shoreWavePhase(float distance,vec2 position,float time) {
    return position.x*2.0+time*1.8+sin(position.y*.65)*.48+sin(position.y*1.7)*.09+.25*exp(-max(distance,0.0)*2.0);
}
float shoreWaveHeight(float distance,vec2 position,float time,float amplitude) {
    float phase=shoreWavePhase(distance,position,time);
    float breaker=sin(phase)+.27*sin(phase*2.0-.65);
    float shoreFade=smoothstep(-.16,.65,distance);
    float offshoreFade=mix(1.0,.55,smoothstep(3.0,14.0,distance));
    return offshoreFade*amplitude*(breaker*.85+sin(position.x*6.3-position.y*2.1+time*2.3)*.12)*shoreFade;
}


// The authored mesh is dense up to X=14 and |Z|=20, then stretches to the
// horizon. Sub-grid geometry waves there fold into coarse moving triangles.
// Fade displacement before those sparse cells; fine normals remain separate.
float shoreGeometryHeight(float distance,vec2 position,float time,float amplitude) {
    float resolved=(1.0-smoothstep(10.0,14.0,position.x))
        *(1.0-smoothstep(14.0,20.0,abs(position.y)));
    return shoreWaveHeight(distance,position,time,amplitude)*resolved;
}
