// Run via eve_eval: (function(){ dofile("verify.nut"); return verifyRiverside(); })()
function verifyRiverside() {
    local checks = 0;
    function requireScene(condition, message) { if (!condition) throw message; }
    requireScene(actors.len() == 3, "expected three actors");
    requireScene(houseModel.getMeshCount() == 10, "cottage material groups missing");
    local textured=0;
    foreach (part in houseParts) if (part.getMaterial().getAlbedoTexture()!=null) ++textured;
    requireScene(textured == 4, "OBJ external material textures did not resolve");
    checks+=2;
    foreach (a in actors) {
        local savedX=a.x, savedZ=a.z, savedDirection=a.direction;
        try {
            foreach (direction, velocity in [[0.0,70.0],[70.0,0.0],[0.0,-70.0],[-70.0,0.0]]) {
                a.x=200.0; a.z=260.0; a.moving=false;
                for (local tick=0;tick<20;++tick) moveActor(a,velocity[0],velocity[1],1.0/60.0);
                requireScene(a.direction==direction,"wrong direction: "+a.id);
                requireScene(a.sprite.isPlaying(),"walk clip did not start: "+a.id);
                requireScene(a.sprite.getFrameIndex()>=direction*6 && a.sprite.getFrameIndex()<direction*6+6,"frame escaped direction row");
                requireScene(a.x!=200.0 || a.z!=260.0,"actor failed to move");
                moveActor(a,0.0,0.0,1.0/60.0);
                requireScene(!a.sprite.isPlaying(),"idle failed to stop clip");
                checks+=5;
            }
            a.x=400.0; a.z=300.0;
            for(local tick=0;tick<180;++tick) moveActor(a,70.0,0.0,1.0/60.0);
            requireScene(a.x<410.1,"river collision failed"); checks++;
            a.x=200.0; a.z=180.0;
            for(local tick=0;tick<120;++tick) moveActor(a,0.0,-70.0,1.0/60.0);
            requireScene(a.z>=166.0,"raised platform collision failed"); checks++;
            a.x=400.0; a.z=240.0;
            for(local tick=0;tick<180;++tick) moveActor(a,70.0,0.0,1.0/60.0);
            requireScene(a.x>590.0,"bridge crossing failed"); checks++;
        } catch(error) {
            a.x=savedX; a.z=savedZ; moveActor(a,0.0,0.0,0.0);
            throw error;
        }
        a.x=savedX; a.z=savedZ; a.direction=savedDirection;
        moveActor(a,0.0,0.0,0.0);
    }
    return "HD2D_SCENE_PASS checks="+checks+" actors=3 directions=4 river=blocked platform=blocked bridge=walkable";
}
