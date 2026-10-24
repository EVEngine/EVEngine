function verifySynthesis() {
    local signatures = [];
    for (local effect = 0; effect < 3; ++effect) {
        local whole = ChipVoice(effect, 1);
        local expected = whole.nextBlock(whole.length);
        local blocked = ChipVoice(effect, 1);
        local cursor = 0, energy = 0.0, signature = 0.0;
        while (cursor < blocked.length) {
            local block = blocked.nextBlock(337);
            foreach (v in block) {
                assert(v == v && v >= -1.0 && v <= 1.0);
                assert(v == expected[cursor]);
                energy += v * v;
                signature += v * ((cursor % 97) + 1);
                cursor++;
            }
        }
        assert(cursor == whole.length && energy > 1.0);
        assert(blocked.nextBlock(1024).len() == 0);
        signatures.append(signature);
    }
    assert(signatures[0] != signatures[1] && signatures[1] != signatures[2]);
    local first = ChipVoice(2, 1).nextBlock(2048);
    local second = ChipVoice(2, 7).nextBlock(2048);
    local differs = false;
    for (local i = 0; i < first.len(); ++i) if (first[i] != second[i]) differs = true;
    assert(differs);
    print("PCM_SYNTHESIS_PASS bounded non-silent deterministic block-independent seeded-noise\n");
}
