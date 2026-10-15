local make_state = function() {
    return { schema = "tutorial.collect", version = 1, x = 100.0, y = 220.0, score = 0 }
}
export function new_state() { return make_state() }

local find_target = function(score) {
    local points = [{ x = 400.0, y = 220.0 }, { x = 720.0, y = 380.0 }, { x = 160.0, y = 400.0 }]
    return points[score % points.len()]
}
export function target_at(score) { return find_target(score) }

export function update_state(state, dx, dy, dt, width, height) {
    if (dx != 0.0 && dy != 0.0) { dx *= 0.70710678; dy *= 0.70710678 }
    local x = state.x + dx * 220.0 * dt
    local y = state.y + dy * 220.0 * dt
    state.x = x < 0.0 ? 0.0 : (x > width - 32.0 ? width - 32.0 : x)
    state.y = y < 0.0 ? 0.0 : (y > height - 32.0 ? height - 32.0 : y)
    local target = find_target(state.score)
    if (state.x < target.x + 24.0 && state.x + 32.0 > target.x &&
        state.y < target.y + 24.0 && state.y + 32.0 > target.y && state.score < 1000000)
        state.score += 1
}

// A strict teaching format: schema|version|score. Extra fields are rejected.
// Position is deliberately transient across process restarts.
export function encode_save(state) {
    return "tutorial.collect|1|" + state.score
}

export function decode_save(text) {
    local fields = split(text, "|")
    if (fields.len() != 3 || fields[0] != "tutorial.collect" || fields[1] != "1")
        throw "unsupported save schema or version"
    local score = fields[2].tointeger()
    if (score < 0 || score > 1000000 || fields[2] != score.tostring() ||
        text != "tutorial.collect|1|" + score)
        throw "invalid save score"
    local candidate = make_state()
    candidate.score = score
    return candidate
}
