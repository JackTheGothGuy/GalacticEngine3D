// title_image.frag
// Draws the title screen image as a full-screen quad.
// The image is blended over whatever was already drawn (the animated bg).
// uAlpha controls the overall fade (used for the fade-out transition).
#version 330 core
in vec2 vUV;
uniform sampler2D uImage;
uniform float     uAlpha;   // overall fade multiplier [0..1]
out vec4 FragColor;

void main()
{
    vec4 col = texture(uImage, vUV);
    FragColor = vec4(col.rgb, col.a * uAlpha);
}
