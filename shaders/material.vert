#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;
layout(location=3) in vec4 aColor;
layout(location=4) in vec3 aTangent;
layout(location=5) in vec3 aBitangent;

uniform mat4  uMVP;
uniform mat4  uModel;
uniform mat4  uNormalMat;
uniform float uTime;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;
out mat3 vTBN;

void main()
{
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal   = normalize(mat3(uNormalMat) * aNormal);
    vUV       = aUV;
    vColor    = aColor;

    vec3 T = normalize(mat3(uModel) * aTangent);
    vec3 B = normalize(mat3(uModel) * aBitangent);
    vec3 N = vNormal;
    vTBN = mat3(T, B, N);

    gl_Position = uMVP * vec4(aPos, 1.0);
}
