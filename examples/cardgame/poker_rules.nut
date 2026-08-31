// Texas Hold'em rules shared by the playable cardgame example.
// Cards are lightweight tables: { id = string, suit = string, rank = 2..14 }.

function pokerMin(a, b) {
    return a < b ? a : b;
}

function pokerMax(a, b) {
    return a > b ? a : b;
}

function pokerRankCount(counts, rank) {
    return rank in counts ? counts[rank] : 0;
}

function pokerStraightHigh(flags) {
    for (local high = 14; high >= 5; high -= 1) {
        local complete = true;
        for (local offset = 0; offset < 5; offset += 1) {
            local rank = high - offset;
            if (rank == 1) rank = 14; // Ace plays low in A-2-3-4-5.
            if (!(rank in flags)) {
                complete = false;
                break;
            }
        }
        if (complete) return high;
    }
    return 0;
}

function pokerCategoryName(category, high) {
    if (category == 8) return high == 14 ? "皇家同花顺" : "同花顺";
    if (category == 7) return "四条";
    if (category == 6) return "葫芦";
    if (category == 5) return "同花";
    if (category == 4) return "顺子";
    if (category == 3) return "三条";
    if (category == 2) return "两对";
    if (category == 1) return "一对";
    return "高牌";
}

function pokerResult(category, kickers) {
    return {
        category = category
        kickers = kickers
        name = pokerCategoryName(category, kickers.len() > 0 ? kickers[0] : 0)
    };
}

function pokerEvaluate(cards) {
    local counts = {};
    local flags = {};
    local suitCounts = {};
    local suitFlags = {};

    foreach (c in cards) {
        if (!(c.rank in counts)) counts.rawset(c.rank, 0);
        counts[c.rank] += 1;
        flags.rawset(c.rank, true);
        if (!(c.suit in suitCounts)) {
            suitCounts.rawset(c.suit, 0);
            suitFlags.rawset(c.suit, {});
        }
        suitCounts[c.suit] += 1;
        suitFlags[c.suit].rawset(c.rank, true);
    }

    foreach (suit, count in suitCounts) {
        if (count >= 5) {
            local straightFlush = pokerStraightHigh(suitFlags[suit]);
            if (straightFlush > 0) return pokerResult(8, [straightFlush]);
        }
    }

    for (local rank = 14; rank >= 2; rank -= 1) {
        if (pokerRankCount(counts, rank) == 4) {
            local kicker = 0;
            for (local k = 14; k >= 2; k -= 1) {
                if (k != rank && pokerRankCount(counts, k) > 0) {
                    kicker = k;
                    break;
                }
            }
            return pokerResult(7, [rank, kicker]);
        }
    }

    local trip = 0;
    local pairForFullHouse = 0;
    for (local rank = 14; rank >= 2; rank -= 1) {
        local count = pokerRankCount(counts, rank);
        if (trip == 0 && count >= 3) trip = rank;
        else if (trip != 0 && count >= 2) {
            pairForFullHouse = rank;
            break;
        }
    }
    if (trip > 0 && pairForFullHouse > 0)
        return pokerResult(6, [trip, pairForFullHouse]);

    foreach (suit, count in suitCounts) {
        if (count >= 5) {
            local flushRanks = [];
            for (local rank = 14; rank >= 2 && flushRanks.len() < 5; rank -= 1) {
                if (rank in suitFlags[suit]) flushRanks.push(rank);
            }
            return pokerResult(5, flushRanks);
        }
    }

    local straight = pokerStraightHigh(flags);
    if (straight > 0) return pokerResult(4, [straight]);

    if (trip > 0) {
        local kickers = [trip];
        for (local rank = 14; rank >= 2 && kickers.len() < 3; rank -= 1) {
            if (rank != trip && pokerRankCount(counts, rank) > 0) kickers.push(rank);
        }
        return pokerResult(3, kickers);
    }

    local pairs = [];
    for (local rank = 14; rank >= 2; rank -= 1) {
        if (pokerRankCount(counts, rank) >= 2) pairs.push(rank);
    }
    if (pairs.len() >= 2) {
        local kicker = 0;
        for (local rank = 14; rank >= 2; rank -= 1) {
            if (rank != pairs[0] && rank != pairs[1] && pokerRankCount(counts, rank) > 0) {
                kicker = rank;
                break;
            }
        }
        return pokerResult(2, [pairs[0], pairs[1], kicker]);
    }

    if (pairs.len() == 1) {
        local kickers = [pairs[0]];
        for (local rank = 14; rank >= 2 && kickers.len() < 4; rank -= 1) {
            if (rank != pairs[0] && pokerRankCount(counts, rank) > 0) kickers.push(rank);
        }
        return pokerResult(1, kickers);
    }

    local highs = [];
    for (local rank = 14; rank >= 2 && highs.len() < 5; rank -= 1) {
        if (pokerRankCount(counts, rank) > 0) highs.push(rank);
    }
    return pokerResult(0, highs);
}

function pokerCompare(left, right) {
    if (left.category != right.category)
        return left.category > right.category ? 1 : -1;
    local count = pokerMax(left.kickers.len(), right.kickers.len());
    for (local i = 0; i < count; i += 1) {
        local a = i < left.kickers.len() ? left.kickers[i] : 0;
        local b = i < right.kickers.len() ? right.kickers[i] : 0;
        if (a != b) return a > b ? 1 : -1;
    }
    return 0;
}

function pokerPreflopStrength(hole) {
    if (hole.len() < 2) return 0.0;
    local high = pokerMax(hole[0].rank, hole[1].rank);
    local low = pokerMin(hole[0].rank, hole[1].rank);
    local strength = (high.tofloat() - 2.0) / 16.0;
    if (high == low) strength += 0.32 + high.tofloat() / 45.0;
    if (hole[0].suit == hole[1].suit) strength += 0.08;
    local gap = high - low;
    if (gap == 1) strength += 0.08;
    else if (gap == 2) strength += 0.04;
    if (high >= 13 && low >= 10) strength += 0.10;
    return pokerMin(strength, 1.0);
}

function pokerMadeHandStrength(cards) {
    if (cards.len() < 5) return pokerPreflopStrength(cards);
    local result = pokerEvaluate(cards);
    local categoryWeight = result.category.tofloat() / 8.0;
    local highWeight = result.kickers.len() > 0 ? result.kickers[0].tofloat() / 70.0 : 0.0;
    return pokerMin(1.0, categoryWeight * 0.88 + highWeight);
}

function pokerTestCard(rank, suit) {
    return { id = suit + rank, rank = rank, suit = suit };
}

function pokerRulesSelfTest() {
    local royal = pokerEvaluate([
        pokerTestCard(14, "H"), pokerTestCard(13, "H"), pokerTestCard(12, "H"),
        pokerTestCard(11, "H"), pokerTestCard(10, "H"), pokerTestCard(2, "C"),
        pokerTestCard(3, "D")
    ]);
    if (royal.category != 8 || royal.kickers[0] != 14) throw "poker test: royal flush";

    local wheel = pokerEvaluate([
        pokerTestCard(14, "S"), pokerTestCard(2, "H"), pokerTestCard(3, "C"),
        pokerTestCard(4, "D"), pokerTestCard(5, "S"), pokerTestCard(9, "H"),
        pokerTestCard(12, "C")
    ]);
    if (wheel.category != 4 || wheel.kickers[0] != 5) throw "poker test: wheel straight";

    local fullHouse = pokerEvaluate([
        pokerTestCard(13, "S"), pokerTestCard(13, "H"), pokerTestCard(13, "C"),
        pokerTestCard(9, "D"), pokerTestCard(9, "S"), pokerTestCard(9, "H"),
        pokerTestCard(2, "C")
    ]);
    if (fullHouse.category != 6 || fullHouse.kickers[0] != 13 || fullHouse.kickers[1] != 9)
        throw "poker test: full house ordering";

    local aces = pokerResult(1, [14, 12, 10, 8]);
    local kings = pokerResult(1, [13, 14, 12, 10]);
    if (pokerCompare(aces, kings) <= 0) throw "poker test: pair comparison";
    return true;
}
