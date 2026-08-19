// Tensor module demo — AITemplate-style compile pipeline in action.
//
//   eve run examples/tensor
//
// Shows four of the intended workloads on one window:
//   1. game-AI policy net (matmul+bias+relu+softmax, compiled once)
//   2. terrain heightfield smoothing (conv2d)
//   3. speech-style attention step (fused SDPA)
//   4. batched simulation update (broadcast arithmetic)

if (!("tf" in getroottable()))
    tf <- null;
if (!("policy" in getroottable()))
    policy <- null;
if (!("frame" in getroottable()))
    frame <- 0;

eve_init = function() {
    gfx.setBackgroundColor(0.06, 0.08, 0.12, 1.0);
    if (tf == null) tf = eve.TF();
    tf.setRandomSeed(42);

    // ---- 1. game-AI policy: 4 inputs -> 2 action probabilities -----------
    local W1 = tf.randomNormal2(4, 3);
    local b1 = tf.fill1(3, 0.1);
    local W2 = tf.randomNormal2(3, 2);
    local fn = tf.func();
    local x = fn.input1(4);
    local h = tf.matmul(tf.reshape2(x, 1, 4), W1);
    h = tf.add(h, b1);
    h = tf.relu(h);
    h = tf.matmul(h, W2);
    h = tf.softmax(h, 1);
    fn.setOutput(h);
    policy = fn.compile();
    print("[tensor] policy device = " + policy.getDevice() + "\n");

    // ---- 2. terrain heightfield: 8x8 -> conv(3x3 ones) -> 6x6 smooth -------
    local height = tf.zeros4(1, 1, 8, 8);
    for (local i = 0; i < 64; i += 1)
        height.set(i, ((i * 37) % 11) * 0.3);   // cheap deterministic noise
    local kernel = tf.fill4(1, 1, 3, 3, 1.0 / 9.0);
    local smooth = tf.conv2d(height, kernel, 1, 0);
    print("[tensor] terrain smoothed 8x8 -> " + smooth.getDim2() + "x" +
          smooth.getDim3() + "\n");

    // ---- 3. speech-style attention: q/k/v [1,1,2,4] -----------------------
    local q = tf.randomNormal4(1, 1, 2, 4);
    local k = tf.randomNormal4(1, 1, 3, 4);
    local v = tf.randomNormal4(1, 1, 3, 4);
    local att = tf.sdpa(q, k, v, 1.0 / 2.0);
    print("[tensor] attention out row0 sum ~= " +
          (att.get4(0,0,0,0)+att.get4(0,0,0,1)+att.get4(0,0,0,2)+att.get4(0,0,0,3)) +
          " (logits, not normalized)\n");

    // ---- 4. batched simulation: 64 agents, 2 state dims --------------------
    local state = tf.randomNormal2(64, 2);
    local dt = 0.016;
    state = tf.add(state, tf.mulScalar(state, dt));
    state = tf.clamp(state, -100.0, 100.0);
    print("[tensor] simulation mean speed = " + tf.reduceMean(state) + "\n");

    print("[tensor] demo ready (run eve update to step the policy)\n");
}

eve_update = function(dt) {
    frame += 1;
    if (policy == null) return;
    // Synthetic observation; run the compiled graph (GPU when available).
    local obs = tf.randomNormal1(4);
    local action = policy.run1(obs);
    if (frame % 60 == 0)
        print("[tensor] policy action probs = " + action.get2(0, 0) + ", " +
              action.get2(0, 1) + "\n");
}

eve_render = function() {
    // Pure compute demo — nothing to draw.
}
