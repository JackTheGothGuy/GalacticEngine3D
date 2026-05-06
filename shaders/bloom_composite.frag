#version 330 core
in vec2 vUV;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform float     uStrength;
out vec4 FragColor;

vec3 aces(vec3 x)
{
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 scene = texture(uScene, vUV).rgb;
    vec3 bloom = texture(uBloom, vUV).rgb * uStrength;
    vec3 col   = aces(scene + bloom);
    col = pow(col, vec3(1.0 / 2.2)); // linear -> sRGB
    FragColor = vec4(col, 1.0);
}
