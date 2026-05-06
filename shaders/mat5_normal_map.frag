// Material 5 — Normal-mapped texture (tangent-space TBN)
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;
in mat3 vTBN;

uniform sampler2D uDiffuse;
uniform sampler2D uNormalMap;
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

    // Unpack normal map and transform to world space via TBN
    vec3 nmap = texture(uNormalMap, vUV * 3.0).rgb * 2.0 - 1.0;
    vec3 N    = normalize(vTBN * nmap);

    float d    = max(dot(N, uLightDir), 0.0);
    vec3  V    = normalize(uCamPos - vWorldPos);
    vec3  H    = normalize(uLightDir + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * 0.6;

    const float ambient = 0.2;
    FragColor = vec4(base * (ambient + (1.0 - ambient) * d) + vec3(spec), 1.0);
}
