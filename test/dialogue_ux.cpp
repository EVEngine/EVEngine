#include "dialogue/DialogueUX.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::dialogue;

TEST_CASE("dialogueUX.historyRichTextAndReadState") {
    DialogueUX* ux = DialogueUX::create();
    ux->clearHistory();
    ux->setSkipMode("off");
    ux->record("intro.1", "Alice", "Hello [pause=0.2][color=red]world[/color]!");
    CHECK(ux->getHistoryCount() == 1);
    CHECK(ux->getHistoryText(0) == "Hello world!");
    CHECK(ux->getTextActionCount("[speed=20]Hi[shake]!") == 2);
    CHECK(ux->isRead("intro.1"));
    CHECK(ux->setSkipMode("read"));
    CHECK(ux->shouldSkip("intro.1"));
    CHECK(!ux->shouldSkip("intro.2"));
}

TEST_CASE("dialogueUX.autoAdvanceWaitsForVoice") {
    DialogueUX* ux = DialogueUX::create();
    ux->setAutoMode(true);
    ux->setAutoDelay(0.5f);
    ux->resetAutoTimer();
    CHECK(!ux->updateAuto(1.f, true));
    CHECK(!ux->updateAuto(0.25f, false));
    CHECK(ux->updateAuto(0.25f, false));
}
