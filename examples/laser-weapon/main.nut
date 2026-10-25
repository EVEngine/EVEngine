// A procedural side-view VFX study. Clock and input belong to this example.
local shader=null, white=null, font=null, age=0.0, paused=false, automatic=true;
local previous={Space=false,P=false,A=false};
function pressed(key) {
    local down=keyboard.isDown(key), edge=down && !previous[key];
    previous[key]=down; return edge;
}
eve_init=function() {
    shader=gfx.newShaderFromSpvFile("shaders/laser.frag.spv");
    shader.declareFloat("time");
    white=gfx.newTexture(eve.Image().newImageDataFromFile("white.tga"),false,false);
    font=gfx.newFont(eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf",24),
        " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/|:.-");
    print("LASER_WEAPON_READY charge=0.9 sustain=1.3 cooldown=1.4 procedural=true\n");
};
eve_update=function(dt) {
    if(pressed("P")) paused=!paused;
    if(pressed("A")) automatic=!automatic;
    if(pressed("Space")) { age=0.0; automatic=false; }
    if(!paused) { age+=dt; if(automatic && age>=3.6) age=age%3.6; }
    shader.sendFloat("time",age);
};
eve_render=function() {
    gfx.clear(); gfx.setShader(shader);
    gfx.drawTexturedRect(white,0.0,0.0,1280.0,720.0,1.0,1.0,1.0,1.0);
    gfx.setShader(null);
    gfx.drawText(font,"H E L I O S",42.0,34.0,0.86,0.95,1.0,1.0,1.4);
    gfx.drawText(font,"DIRECTED ENERGY / PULSE CANNON",44.0,82.0,0.35,0.59,0.68,1.0,0.58);
    local phase=age<0.9?"01 / CHARGING":(age<2.2?"02 / DISCHARGE":"03 / COOLING");
    gfx.drawText(font,paused?"PAUSED":phase,970.0,48.0,0.42,0.9,1.0,1.0,0.68);
    gfx.drawText(font,"SPACE  Fire    P  Freeze    A  Auto loop",44.0,660.0,0.43,0.58,0.66,1.0,0.68);
    gfx.drawText(font,automatic?"AUTO / ON":"MANUAL",1080.0,660.0,0.4,0.8,0.9,1.0,0.65);
};
