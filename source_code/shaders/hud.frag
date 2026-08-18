#version 450

//font.png keeps its glyphs in the alpha channel, so the sample is a mask and
//the colour comes from here rather than from the texture
layout(binding = 1) uniform sampler2D font;

layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

const vec3 HUD_COLOR = vec3(0.72, 0.95, 1.0);

void main() {
    float glyph = texture(font, in_uv).a;

    if (glyph < 0.35)
        discard;

    out_color = vec4(HUD_COLOR * glyph, 1.0);
}
