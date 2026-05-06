#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform float     uThreshold;
out vec4 FragColor;

void main()
{
    vec3  c    = texture(uScene, vUV).rgb;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float knee = 0.1;
    float remap = smoothstep(uThreshold - knee, uThreshold + knee, luma);
    FragColor = vec4(c * remap, 1.0);
}
