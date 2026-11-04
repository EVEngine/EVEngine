// A local gameplay entity; the native World3D owns its simulated transform.
class TrialRunner extends eve.Entity { body = null; }
site <- null;
state <- {};
intent <- {x=0.0,z=0.0,jump=false};
previous <- {};
fontData <- null;
hudFont <- null;
function min3(a,b) { return a<b?a:b; }
function max3(a,b) { return a>b?a:b; }
function clamp3(v,a,b) { return max3(a,min3(b,v)); }
function pressed3(k) {
    local down=keyboard.isDown(k), old=k in previous?previous[k]:false;
    previous[k] <- down; return down && !old;
}
dofile("course.nut");
dofile("simulation.nut");
dofile("presentation.nut");
game <- {};
game.start <- function() { site.overview=false; };
game.overview <- function() { site.overview=true; };
game.north <- function() { intent.x=0.0; intent.z=1.0; };
game.south <- function() { intent.x=0.0; intent.z=-1.0; };
game.east <- function() { intent.x=1.0; intent.z=0.0; };
game.west <- function() { intent.x=-1.0; intent.z=0.0; };
game.stop <- function() { intent.x=0.0; intent.z=0.0; };
game.jump <- function() { intent.jump=true; };
game.retry <- function() { returnToCheckpoint(false); };
game.reset <- function() { resetCourse(); };
eve_init <- function() {
    gfx.setBackgroundColor(0.15,0.23,0.31,1.0);
    fontData=eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf",28);
    hudFont=gfx.newFont(fontData," ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/.:+-[]()%");
    createCourse(); resetCourse();
};
eve_update <- function(dt) {
    if(pressed3("return") || pressed3("enter")) site.overview=false;
    if(pressed3("f")) site.overview=!site.overview;
    if(pressed3("r")) returnToCheckpoint(false);
    if(pressed3("t")) resetCourse();
    if(pressed3("space") || intent.jump) { site.jumpBuffer=0.16; intent.jump=false; }
    if(!site.overview) {
        if(keyboard.isDown("q")) site.yaw-=dt*1.8;
        if(keyboard.isDown("e")) site.yaw+=dt*1.8;
    }
    site.remainder+=min3(dt,0.05);
    while(site.remainder>=1.0/120.0) { site.remainder-=1.0/120.0; simulateCourse(1.0/120.0); }
    presentCourse(dt);
};
eve_render <- function() { gfx.clear(); gfx.render3D(); drawCourseHud(); };
