// Material 7 — Schlick Fresnel with animated colour pulse
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
uniform float     uFresnelPow;
uniform float     uTime;

out vec4 FragColor;

void main()
{
    vec3 base = uShowTex ? texture(uDiffuse, vUV).rgb : uTint;
    if (uShowVC) base *= vColor.rgb;

    vec3  N        = normalize(vNormal);
    vec3  V        = normalize(uCamPos - vWorldPos);
    float cosTheta = max(dot(N, V), 0.0);

    // Schlick approximation
    float fres = pow(1.0 - cosTheta, uFresnelPow);

    // Animate the Fresnel tint colour slightly
    float pulse      = 0.5 + 0.5 * sin(uTime * 0.8);
    vec3  fresnelCol = mix(vec3(0.25, 0.50, 1.0), vec3(0.8, 0.95, 1.0), fres * pulse);

    float d       = max(dot(N, uLightDir), 0.0);
    const float ambient = 0.2;
    vec3  lit     = base * (ambient + (1.0 - ambient) * d);
    vec3  col     = mix(lit, fresnelCol, fres * 0.85);

    FragColor = vec4(col, 1.0);
}
