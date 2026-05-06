// Material 4 — Lit texture with HDR-ready emissive rim (bloom-compatible)
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;

uniform sampler2D uDiffuse;
uniform vec3      uLightDir;
uniform vec3      uCamPos;
uniform bool      uShowTex;
uniform bool      uShowVC;
uniform vec3      uTint;
uniform float     uTime;

out vec4 FragColor;

void main()
{
    vec3 base = uShowTex ? texture(uDiffuse, vUV).rgb : uTint;
    if (uShowVC) base *= vColor.rgb;

    vec3  N    = normalize(vNormal);
    float d    = max(dot(N, uLightDir), 0.0);
    vec3  V    = normalize(uCamPos - vWorldPos);
    vec3  H    = normalize(uLightDir + V);

    // High specular — pops with bloom
    float spec = pow(max(dot(N, H), 0.0), 128.0) * 3.5;

    // Pulsing emissive rim
    float pulse   = 0.5 + 0.5 * sin(uTime * 1.5);
    float rim     = pow(1.0 - max(dot(N, V), 0.0), 4.0) * 1.5 * pulse;
    vec3  emissive = uTint * rim * 1.5;

    const float ambient = 0.15;
    vec3 col = base * (ambient + (1.0 - ambient) * d) + vec3(spec) + emissive;
    FragColor = vec4(col, 1.0);
}
