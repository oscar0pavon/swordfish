#ifndef VERTEX_H
#define VERTEX_H

#include "../renderer/cglm/cglm.h"

typedef struct Vertex {
  vec3 position;
  vec3 color;
  vec3 normal;
  vec2 uv;
  vec4 joint;
  vec4 weight;
  long unsigned int id;
  bool selected;
} PVertex;

#endif // !VERTEX_H
