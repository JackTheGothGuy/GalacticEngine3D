#version 330 core
layout(location=0) in vec2 aPos;
uniform vec4 uRect; // x y w h in NDC (lower-left origin, full extents)
out vec2 vUV;
void main()
{
    vec2 p = uRect.xy + aPos * uRect.zw;
    vUV = aPos;
    gl_Position = vec4(p, 0.0, 1.0);
}
