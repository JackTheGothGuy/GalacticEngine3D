#version 330 core
in vec3 vDir;
out vec4 FragColor;

void main()
{
    float t = clamp(vDir.y, 0.0, 1.0);
    // GCN-era: deep purple-blue at zenith, warm orange-amber at horizon
    vec3 zenith  = vec3(0.06, 0.04, 0.18);
    vec3 midsky  = vec3(0.12, 0.18, 0.45);
    vec3 horizon = vec3(0.55, 0.30, 0.12);
    vec3 col = mix(
        mix(horizon, midsky, smoothstep(0.0, 0.3, t)),
        zenith,
        smoothstep(0.3, 1.0, t)
    );
    FragColor = vec4(col, 1.0);
}
