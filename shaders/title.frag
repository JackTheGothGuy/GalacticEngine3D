#version 330 core
in vec2 vUV;
uniform float uTime;
out vec4 FragColor;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float noise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main()
{
    vec2 uv = vUV;

    // Animated dark sky background
    float t  = uTime * 0.12;
    float n1 = noise(uv * 3.0 + t);
    float n2 = noise(uv * 6.0 - t * 1.3);
    float n  = n1 * 0.6 + n2 * 0.4;

    vec3 c0 = vec3(0.02, 0.01, 0.06);
    vec3 c1 = vec3(0.07, 0.04, 0.22);
    vec3 c2 = vec3(0.15, 0.06, 0.35);
    vec3 bg  = mix(mix(c0, c1, n), c2, n * n) * 1.2;

    // Stars
    float star = pow(max(0.0, hash(floor(uv * 120.0)) - 0.97) / 0.03, 2.0);
    star *= 0.5 + 0.5 * sin(uTime * 2.5 + hash(floor(uv * 120.0)) * 6.28);
    bg += vec3(star) * 0.8;

    // Horizon glow (warm amber)
    float horizGlow = exp(-abs(uv.y - 0.32) * 12.0) * 0.6;
    bg += vec3(0.4, 0.18, 0.04) * horizGlow;

    FragColor = vec4(bg, 1.0);
}
