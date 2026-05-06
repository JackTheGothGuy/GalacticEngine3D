#version 330 core
in vec2 vUV;
uniform vec4  uColor;
uniform float uHover;
out vec4 FragColor;

void main()
{
    vec2  uv      = vUV * 2.0 - 1.0;
    float border  = max(abs(uv.x), abs(uv.y));
    float outline = smoothstep(0.92, 0.88, border);
    float fill    = smoothstep(0.88, 0.85, border);
    vec3  col     = mix(uColor.rgb, uColor.rgb * 1.4 + 0.05, uHover);
    FragColor = vec4(
        col * fill + vec3(0.9, 0.8, 0.5) * outline * (1.0 - fill),
        (fill + outline * 0.5) * uColor.a
    );
}
