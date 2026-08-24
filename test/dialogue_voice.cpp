#include "dialogue/DialogueVoice.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::dialogue;

TEST_CASE("dialogueVoice.localizedDurationAndEnvelope") {
    DialogueVoice* voice = DialogueVoice::create();
    voice->clear();
    voice->setEnvelopeRate(4.f);
    CHECK(voice->registerClip("intro.1", "en", 1.f, "0,0.25,0.75,1"));
    CHECK(voice->registerClip("intro.1", "", 0.5f, "0.1,0.2"));
    CHECK(voice->play("intro.1", "en"));
    voice->update(0.5f);
    CHECK(voice->getAmplitude() == 0.75f);
    CHECK(voice->isPlaying());
    voice->update(0.5f);
    CHECK(!voice->isPlaying());
    CHECK(voice->shouldAutoAdvance());
    CHECK(voice->play("intro.1", "zh"));
    CHECK(voice->getDuration() == 0.5f);
}
