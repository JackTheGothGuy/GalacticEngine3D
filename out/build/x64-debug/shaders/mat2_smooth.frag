// Material 2 — Smooth-shaded texture with Blinn-Phong specular
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

out vec4 FragColor;

void main()
{
    vec3 base = uShowTex ? texture(uDiffuse, vUV).rgb : uTint;
    if (uShowVC) base *= vColor.rgb;

    vec3  N    = normalize(vNormal);
    float d    = max(dot(N, uLightDir), 0.0);
    vec3  V    = normalize(uCamPos - vWorldPos);
    vec3  H    = normalize(uLightDir + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.3;

    const float ambient = 0.25;
    FragColor = vec4(base * (ambient + (1.0 - ambient) * d) + vec3(spec), 1.0);
}
