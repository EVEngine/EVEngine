// Works in the engine after eve.Agent is exposed. No tensor module required.
local brain = eve.Agent();
local environment = {
    x = 0,
    observe = function(reward) {
        return { features = [x / 3.0], legalActions = [0, 1], reward = reward,
                 outcome = x == 3 ? "success" : "running" };
    },
    reset = function(seed) { // seed is a lossless decimal string
        x = 0;
        return observe(0.0);
    },
    step = function(action, dt) {
        local before = x;
        x += action == 0 ? 1 : -1;
        if (x < 0) x = 0;
        if (x > 3) x = 3;
        return observe((x - before).tofloat());
    }
};
local trained = brain.run({featureCount=1, actionCount=2, population=8,
    elites=2, generations=4, horizon=8, environmentSeed="42"}, environment);
if (!trained.ok) throw "agent training failed: " + trained.code;
agentExampleReport <- trained.value;
local state = environment.reset("42");
local action = brain.act(trained.value.policy, state);
if (!action.ok) throw "agent inference failed";
local replayed = brain.replay(trained.value.best, environment, 0.00001);
if (!replayed.ok) throw "agent replay failed";
