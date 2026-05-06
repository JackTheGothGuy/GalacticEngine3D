#version 330 core
out vec2 vUV;
void main()
{
    // Generates a fullscreen triangle from gl_VertexID (no VBO needed)
    vec2 p = vec2(
        (gl_VertexID & 1) * 4.0 - 1.0,
        (gl_VertexID & 2) * 2.0 - 1.0
    );
    vUV = p * 0.5 + 0.5;
    gl_Position = vec4(p, 0.0, 1.0);
}
