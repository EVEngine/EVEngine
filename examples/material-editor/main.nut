// A complete script-composed material studio, backed by the native material document.
persist studio = null;

function skyboxFaces(name) {
    local faces=[];
    foreach (face in ["px","nx","py","ny","pz","nz"])
        faces.append("assets/environments/"+name+"/"+face+".png");
    return faces;
}

eve_init = function() {
    if (!fs.setIdentity("material-editor", true) || !fs.setupWriteDirectory())
        throw "Cannot prepare the material editor save directory";
    studio = createMaterialStudio({
        width=config.width, height=config.height,
        environment="outdoor",
        environments=[
            {id="outdoor",label="Outdoor Sky",faces=skyboxFaces("kloppenheim_06_puresky")},
            {id="photostudio",label="Photo Studio",faces=skyboxFaces("studio_small_09")},
        ],
        screenshot="material-editor.png",
    });
};
eve_update = function(dt) { studio.update(dt); };
eve_render = function() { studio.render(); };
