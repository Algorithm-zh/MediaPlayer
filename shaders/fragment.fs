#version 330 core
out vec4 FragColor;
in vec2 TexCoord;

uniform sampler2D texY;
uniform sampler2D texU;
uniform sampler2D texV;

uniform int screenIndex; // 0: left, 1: center, 2: right
uniform float blendWidth; // Width of the blend area (e.g., 0.15)

void main()
{
    float y = texture(texY, TexCoord).r;
    float u = texture(texU, TexCoord).r - 0.5;
    float v = texture(texV, TexCoord).r - 0.5;
    float r = y + 1.402 * v;
    float g = y - 0.344 * u - 0.714 * v;
    float b = y + 1.772 * u;

    float alpha = 1.0;
    if (screenIndex == 0) { // Left screen
        alpha = 1.0 - smoothstep(1.0 - blendWidth, 1.0, TexCoord.x);
    } else if (screenIndex == 2) { // Right screen
        alpha = smoothstep(0.0, blendWidth, TexCoord.x);
    }
    // Center screen (screenIndex == 1) remains at alpha = 1.0

    FragColor = vec4(r, g, b, alpha);
}
