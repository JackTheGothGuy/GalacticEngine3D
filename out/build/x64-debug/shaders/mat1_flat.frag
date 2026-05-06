// Material 1 — Flat / cel-shaded texture
// Quantises diffuse into 4 bands for a GCN / Twilight Princess look.
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;

uniform sampler2D uDiffuse;
uniform vec3      uLightDir;
uniform bool      uShowTex;
uniform bool      uShowVC;
uniform vec3      uTint;

out vec4 FragColor;

void main()
{
    vec3 base = uShowTex ? texture(uDiffuse, vUV).rgb : uTint;
    if (uShowVC) base *= vColor.rgb;

    // Quantise diffuse into 4 cel-shade bands
    float d = max(dot(normalize(vNormal), uLightDir), 0.0);
    d = floor(d * 4.0) / 4.0;

    const float ambient = 0.3;
    FragColor = vec4(base * (ambient + (1.0 - ambient) * d), 1.0);
}
