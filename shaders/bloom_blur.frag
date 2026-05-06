#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform vec2      uDir; // (1/w,0) for horizontal pass, (0,1/h) for vertical
out vec4 FragColor;

void main()
{
    // 9-tap Gaussian kernel weights
    const float w[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);
    vec3 result = texture(uTex, vUV).rgb * w[0];
    for (int i = 1; i < 5; ++i)
    {
        result += texture(uTex, vUV + uDir * float(i)).rgb * w[i];
        result += texture(uTex, vUV - uDir * float(i)).rgb * w[i];
    }
    FragColor = vec4(result, 1.0);
}
