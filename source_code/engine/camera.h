//
// Created by pavon on 6/23/19.
//

#ifndef CAMERA_H
#define CAMERA_H 

#include "renderer/cglm/cglm.h"
#include "renderer/vulkan.h"


void camera_rotate_control(float yaw, float pitch);
void camera_init(Camera* camera);

void camera_update(Camera* camera);

void camera_update_aspect_ratio(Camera* camera);

void camera_set_position(Camera* camera, vec3 position);

extern float camera_height_screen;
extern float camera_width_screen;
extern versor camera_rotation;

extern bool move_camera_input;

extern float camera_rotate_angle;

#endif //CAMERA_H
