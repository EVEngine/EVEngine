// Defaults are editable here; sliders change shader push constants immediately.
inkKnobs <- [
    {id="edgeStrength",label="Edge strength",value=0.18,min=0.0,max=0.6},
    {id="reflection",label="Reflection",value=1.0,min=0.0,max=3.0},
    {id="roughness",label="Ink roughness",value=0.21,min=0.08,max=0.9},
    {id="textureScale",label="Texture scale",value=1.0,min=0.25,max=4.0}
];
function applyInkParameters() {
    foreach(knob in inkKnobs) inkShader.sendFloat(knob.id,knob.value);
}
function updateInkParameters() {
    ui.select("inkhud");
    foreach(knob in inkKnobs) {
        local value=ui.getValue(knob.id);
        if(value!=knob.value) {
            knob.value=clamp(value,knob.min,knob.max);
            inkShader.sendFloat(knob.id,knob.value);
        }
    }
}
