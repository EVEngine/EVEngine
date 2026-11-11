// Browser audio uses the same Sound, Audio and Source API as desktop.
persist __pg = {}

eve_init = function() {
    if (!("music" in __pg)) {
        __pg.musicData <- sound.newSoundDataFromFile("playground/assets/music_loop.wav");
        __pg.effectData <- sound.newSoundDataFromFile("playground/assets/effect.wav");
        __pg.music <- audio.newSource(__pg.musicData);
        __pg.effect <- audio.newSource(__pg.effectData);
        __pg.music.setVolume(0.25);
        __pg.music.setLooping(true);
    }
    __pg.music.play();
    print("Music queued. Click the page to unlock browser audio. Run __pg.effect.play() in REPL for the sound effect.\n");
};

eve_update = function(dt) {};
eve_render = function() { gfx.clear(); };
