#include "engine2d.h"
#include "engine/vertex.h"


void pe_2d_create_quad(float x, float y, float width, float height){

  PVertex top_left = {.position = {x, y}};
  PVertex top_right = {.position = {x + width, y}};
  PVertex bottom_right = {.position = {x + width, y + height}};
  PVertex bottom_left = {.position = {x, y + height}};


}
