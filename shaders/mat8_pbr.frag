// Material 8 — PBR: GGX specular, Smith geometry, Schlick Fresnel
// Twilight Princess x MGS metallic aesthetic
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;

uniform sampler2D uDiffuse;
uniform vec3      uLightDir;
uniform vec3      uCamPos;
uniform vec3      uLightCol;
uniform bool      uShowTex;
uniform bool      uShowVC;
uniform vec3      uTint;
uniform float     uMetallic;
uniform float     uRoughness;

out vec4 FragColor;

const float PI = 3.14159265;

// GGX / Trowbridge-Reitz NDF
float DistGGX(vec3 N, vec3 H, float r)
{
    float a   = r * r;
    float a2  = a * a;
    float NdH = max(dot(N, H), 0.0);
    float d   = NdH * NdH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Smith + Schlick-GGX geometry term
float GeomSchlick(float NdV, float r)
{
    float k = (r + 1.0);
    k = k * k / 8.0;
    return NdV / (NdV * (1.0 - k) + k);
}

float GeomSmith(vec3 N, vec3 V, vec3 L, float r)
{
    return GeomSchlick(max(dot(N, L), 0.0), r)
         * GeomSchlick(max(dot(N, V), 0.0), r);
}

// Fresnel-Schlick
vec3 FresnelSchlick(float cosA, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosA, 0.0, 1.0), 5.0);
}

void main()
{
    vec3 albedo = uShowTex ? texture(uDiffuse, vUV).rgb : uTint;
    if (uShowVC) albedo *= vColor.rgb;
    albedo = pow(albedo, vec3(2.2)); // sRGB -> linear

    vec3  N   = normalize(vNormal);
    vec3  V   = normalize(uCamPos - vWorldPos);
    vec3  L   = uLightDir;
    vec3  H   = normalize(V + L);
    float NdL = max(dot(N, L), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, uMetallic);
    vec3 F  = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float NDF = DistGGX(N, H, uRoughness);
    float G   = GeomSmith(N, V, L, uRoughness);

    vec3  num      = NDF * G * F;
    float den      = 4.0 * max(dot(N, V), 0.0) * NdL + 0.0001;
    vec3  specular = num / den;

    vec3 kS      = F;
    vec3 kD      = (vec3(1.0) - kS) * (1.0 - uMetallic);
    vec3 diffuse = kD * albedo / PI;

    vec3 radiance = uLightCol * 3.5;
    vec3 ambient  = vec3(0.03) * albedo;
    vec3 col      = ambient + (diffuse + specular) * radiance * NdL;

    // Reinhard tonemap + gamma correction
    col = col / (col + vec3(1.0));
    col = pow(col, vec3(1.0 / 2.2));
    FragColor = vec4(col, 1.0);
}
