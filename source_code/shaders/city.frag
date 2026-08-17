#version 450

//font.png, a 16x16 grid of 32px cells indexed straight by ascii code
layout(binding = 1) uniform sampler2D font;

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 building_color;
layout(location = 2) in float view_depth;
layout(location = 3) in float is_roof;
layout(location = 4) in float time;

layout(location = 5) flat in uvec4 name0;
layout(location = 6) flat in uvec4 name1;
layout(location = 7) flat in float name_length;
layout(location = 8) in vec3 scale;

layout(location = 0) out vec4 out_color;

//character cells in world units, so glyphs stay the same size on a
//short building and a tall one
const vec2 density = vec2(3.0, 2.4);

//how far apart the name bands repeat up a building, in world units
const float NAME_BAND_SPACING = 5.0;

//character size for the name, in world units. names too long for the
//facade shrink below this, everything else renders at exactly this size
const float NAME_CHAR_WIDTH = 0.28;

float hash(vec2 cell) {
    return fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
}

//characters 0-15 live in name0, 16-31 in name1, 4 per uint
uint name_char(int index) {
    uint word = (index < 16) ? name0[index >> 2] : name1[(index - 16) >> 2];
    return (word >> (8u * uint(index & 3))) & 0xFFu;
}

float glyph_alpha(uint code, vec2 local) {
    //atlas row 0 is the top of the png but the wall counts v from the
    //ground up, so the glyph has to be read back upside down
    vec2 glyph_local = vec2(local.x, 1.0 - local.y);

    vec2 atlas =
        (vec2(float(code % 16u), float(code / 16u)) + glyph_local) / 16.0;

    //textureLod, because the grid jumps at every cell edge and the implicit
    //derivative would pick a garbage mip level right on the seam
    return textureLod(font, atlas, 0.0).a;
}

void main() {
    if (is_roof > 0.5) {
        float roof_fog = exp(-view_depth * 0.020);
        out_color = vec4(building_color * 0.30 * roof_fog, 1.0);
        return;
    }

    //the name runs across the facade like a sign, repeated up the tower so
    //there is always one near whatever height you are looking at. characters
    //hold a fixed size and only shrink when a name is too long for the face
    float name_len = max(name_length, 1.0);
    float char_width = min(NAME_CHAR_WIDTH, scale.x / name_len);

    //centre the name on the facade
    float x_in_name = uv.x - (scale.x - char_width * name_len) * 0.5;

    float band_y = mod(uv.y, NAME_BAND_SPACING);
    int char_index = int(floor(x_in_name / char_width));

    bool is_name = band_y < char_width && x_in_name >= 0.0 &&
                   char_index >= 0 && char_index < int(name_length);

    vec2 local;
    uint code;
    float brightness;

    if (is_name) {
        local = vec2(fract(x_in_name / char_width), band_y / char_width);
        code = name_char(char_index);

        brightness = glyph_alpha(code, local) * 2.2;
    } else {
        vec2 base_uv = uv * density;

        //every other cell streams, like the movie facades
        float column = floor(base_uv.x);
        float speed = 0.35 + hash(vec2(column, 7.0)) * 1.4;

        vec2 scroll_uv = vec2(base_uv.x, base_uv.y - time * speed);

        vec2 cell = floor(scroll_uv);
        local = fract(scroll_uv);

        //digits and uppercase are the legible part of the atlas
        code = uint(48.0 + floor(hash(cell) * 43.0));

        //hold some cells dark so the facade is not a solid wall of characters
        float lit = step(0.30, hash(cell + 3.7));

        brightness = glyph_alpha(code, local) * lit * 1.1;
    }

    vec3 tint = building_color;

    //the name burns brighter than the noise around it
    if (is_name)
        tint = mix(building_color, vec3(1.0), 0.5);

    //fade into the dark so the street reads as going on forever
    float fog = exp(-view_depth * 0.020);

    out_color = vec4(tint * (0.04 + brightness) * fog, 1.0);
}
