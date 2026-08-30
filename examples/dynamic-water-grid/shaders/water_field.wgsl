struct FSIn {
    @location(0) color: vec4f,
    @location(1) uv: vec2f,
};

struct Externals {
    data: array<vec4f, 8>,
};

@group(0) @binding(0) var mainTex: texture_2d<f32>;
@group(0) @binding(2) var mainSamp: sampler;
@group(0) @binding(4) var<uniform> u: Externals;

fn p(i: u32) -> f32 {
    return u.data[i / 4u][i % 4u];
}

@fragment
fn fs_main(input: FSIn) -> @location(0) vec4f {
    let screenSize = vec2f(p(0u), p(1u));
    let origin = vec2f(p(2u), p(3u));
    let tileSize = vec2f(p(4u), p(5u));
    let fieldSize = vec2f(p(6u), p(7u));
    let time = p(8u);
    let wetThreshold = p(9u);
    let opacity = p(10u);

    let screen = floor(input.uv * screenSize) + vec2f(0.5);
    let a = (screen.x - origin.x) / max(tileSize.x * 0.5, 0.0001);
    let b = (screen.y - origin.y) / max(tileSize.y * 0.5, 0.0001);
    let grid = vec2f((b + a) * 0.5, (b - a) * 0.5);

    if grid.x < -0.5 || grid.y < -0.5 ||
       grid.x > fieldSize.x - 0.5 || grid.y > fieldSize.y - 0.5 {
        return vec4f(0.0);
    }

    let dataUV = (grid + vec2f(0.5)) / fieldSize;
    let field = textureSampleLevel(mainTex, mainSamp, dataUV, 0.0);
    let depth = field.r;
    let flow = field.gb * 2.0 - 1.0;

    var wave = sin(screen.x * 0.075 + screen.y * 0.031 + time * 2.25 + flow.x * 2.0);
    wave += sin(screen.x * -0.028 + screen.y * 0.093 + time * 1.37 + flow.y * 2.0);
    wave *= 0.5;

    let threshold = wetThreshold + wave * 0.009;
    let coverage = smoothstep(threshold - 0.018, threshold + 0.025, depth);
    if coverage <= 0.001 {
        return vec4f(0.0);
    }

    let deep = smoothstep(0.10, 0.82, depth);
    let shallowColor = vec3f(0.18, 0.55, 0.62);
    let deepColor = vec3f(0.035, 0.20, 0.34);
    var waterColor = mix(shallowColor, deepColor, deep);

    let contour = 1.0 - smoothstep(0.015, 0.065, abs(depth - threshold - 0.035));
    let glint = max(0.0, sin(screen.x * 0.16 - screen.y * 0.07 + time * 3.1));
    waterColor += vec3f(0.42, 0.31, 0.46) * contour * (0.45 + glint * 0.35);

    let alpha = coverage * opacity * mix(0.56, 0.84, deep);
    return vec4f(waterColor * input.color.rgb, alpha * input.color.a);
}
