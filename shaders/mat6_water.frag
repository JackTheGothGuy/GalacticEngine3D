// Material 6 — Water: additive scrolling sparkle + Fresnel rim + Phong specular
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;

uniform sampler2D uDiffuse;  // base water colour (dark blue-green checker)
uniform sampler2D uSparkle;  // high-contrast grayscale sparkle texture
uniform vec3      uLightDir;
uniform vec3      uCamPos;
uniform bool      uShowTex;
uniform bool      uShowVC;
uniform vec3      uTint;
uniform float     uTime;

out vec4 FragColor;

void main()
{
    vec3  N = normalize(vNormal);
    vec3  V = normalize(uCamPos - vWorldPos);

    // Two independently-scrolling sparkle layers
    vec2 scroll1 = vUV + vec2( uTime * 0.06,  uTime * 0.04);
    vec2 scroll2 = vUV + vec2(-uTime * 0.03,  uTime * 0.07);
    float sp1 = texture(uSparkle, scroll1 * 4.0).r;
    float sp2 = texture(uSparkle, scroll2 * 6.0).r;
    float sparkle = pow(max(sp1, sp2), 1.5);

    // Base water tint / texture
    vec3 base = uShowTex ? texture(uDiffuse, vUV).rgb : uTint;
    if (uShowVC) base *= vColor.rgb;

    float d    = max(dot(N, uLightDir), 0.0);
    float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    vec3  waterCol = base * (0.2 + 0.8 * d) + base * fres * 0.4;

    // Additive white sparkle glints
    vec3 sparkleCol = vec3(1.0, 0.95, 0.8) * sparkle * 2.5;

    // Phong specular
    vec3  H    = normalize(uLightDir + V);
    float spec = pow(max(dot(N, H), 0.0), 128.0) * 1.2;

    FragColor = vec4(waterCol + sparkleCol + vec3(spec), 0.88);
}
