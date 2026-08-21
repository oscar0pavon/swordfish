#version 450

//the pointer is shaded, not sampled: the quad has no texture and the arrow is
//the signed distance to the classic seven point polygon, so the edges stay
//antialiased and the shape survives any size the quad is scaled to
layout(location = 0) in vec2 in_uv;

layout(location = 0) out vec4 out_color;

const vec3 CURSOR_FILL = vec3(1.0);
const vec3 CURSOR_OUTLINE = vec3(0.0);

//how far outside the arrow the outline reaches, in uv - the quad is square, so
//this is the same number of pixels in both directions
const float OUTLINE_WIDTH = 0.05;

//tip at (0,0), y running down the way the ortho projection does. the polygon
//only covers the top left of the quad; the rest is the outline's room
const int CURSOR_POINTS = 7;
const vec2 arrow[CURSOR_POINTS] = vec2[CURSOR_POINTS](
    vec2(0.00, 0.00),
    vec2(0.00, 0.80),
    vec2(0.20, 0.62),
    vec2(0.33, 0.95),
    vec2(0.48, 0.88),
    vec2(0.35, 0.57),
    vec2(0.62, 0.55));

//negative inside the polygon, positive outside. the crossing test flips the
//sign once per edge the point is level with, which is a point in polygon test
//riding along with the distance
float arrow_distance(vec2 point) {

  float squared = dot(point - arrow[0], point - arrow[0]);
  float winding = 1.0;

  for (int i = 0, j = CURSOR_POINTS - 1; i < CURSOR_POINTS; j = i, i++) {

    vec2 side = arrow[j] - arrow[i];
    vec2 to_point = point - arrow[i];

    vec2 offset =
        to_point - side * clamp(dot(to_point, side) / dot(side, side), 0.0, 1.0);

    squared = min(squared, dot(offset, offset));

    bvec3 crossing = bvec3(point.y >= arrow[i].y, point.y < arrow[j].y,
                           side.x * to_point.y > side.y * to_point.x);

    if (all(crossing) || all(not(crossing)))
      winding = -winding;
  }

  return winding * sqrt(squared);
}

void main() {

  float signed_distance = arrow_distance(in_uv);

  //one pixel's worth of the distance field, whatever size the quad ended up
  float pixel = fwidth(signed_distance);

  float fill = 1.0 - smoothstep(-pixel, pixel, signed_distance);
  float outline = 1.0 - smoothstep(OUTLINE_WIDTH - pixel, OUTLINE_WIDTH + pixel,
                                   signed_distance);

  if (outline < 0.01)
    discard;

  out_color = vec4(mix(CURSOR_OUTLINE, CURSOR_FILL, fill), outline);
}
