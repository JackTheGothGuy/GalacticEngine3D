// Material 3 — Vertex-colour blended with smooth texture
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
    vec3 base = uTint;
    if (uShowTex) base *= texture(uDiffuse, vUV).rgb;
    if (uShowVC)  base *= vColor.rgb;
    else          base  = mix(uTint, vColor.rgb, 0.85);

    vec3  N    = normalize(vNormal);
    float d    = max(dot(N, uLightDir), 0.0);
    vec3  V    = normalize(uCamPos - vWorldPos);
    vec3  H    = normalize(uLightDir + V);
    float spec = pow(max(dot(N, H), 0.0), 64.0) * 0.2;

    const float ambient = 0.2;
    FragColor = vec4(base * (ambient + (1.0 - ambient) * d) + vec3(spec), 1.0);
}
