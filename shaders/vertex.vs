#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec2 uvOffset;
uniform vec2 uvScale;

// Uniforms for Keystone Correction
uniform int screenIndex;
uniform float keystoneFactor;

void main() {
    vec3 pos = aPos;

    // Apply keystone correction only to side panels
    if (screenIndex == 0) { // Left Panel
        // We want to pinch the right side (where aPos.x is positive)
        // The pinch amount is proportional to how far from the vertical center (aPos.y)
        pos.x -= keystoneFactor * aPos.y * (aPos.x + 1.0) * 0.5;
    } else if (screenIndex == 2) { // Right Panel
        // We want to pinch the left side (where aPos.x is negative)
        pos.x += keystoneFactor * aPos.y * (aPos.x - 1.0) * 0.5;
    }

    gl_Position = projection * view * model * vec4(pos, 1.0);
    TexCoord = aTexCoord * uvScale + uvOffset;
}
