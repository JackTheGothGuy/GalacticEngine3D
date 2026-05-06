#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uVP;
uniform float uRadius;
out vec3 vDir;
void main()
{
    vDir = aPos;
    vec4 clip = uVP * vec4(aPos * uRadius, 1.0);
    gl_Position = clip.xyww;
}
