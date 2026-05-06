#version 330 core
in vec3 vDir;
out vec4 FragColor;

uniform sampler2D uPanorama;

const vec2 INV_ATAN = vec2(0.1591, 0.3183); // 1/(2π), 1/π

vec2 sampleSpherical(vec3 d)
{
    vec3 n = normalize(d);
    vec2 uv = vec2(atan(n.z, n.x), asin(n.y));
    uv *= INV_ATAN;
    uv += 0.5;
    return uv;
}

void main()
{
    vec2 uv = sampleSpherical(vDir);
    FragColor = vec4(texture(uPanorama, uv).rgb, 1.0);
}