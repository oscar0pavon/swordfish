//

#include "camera.h"
#include "renderer/cglm/cam.h"
#include "renderer/cglm/types.h"
#include "renderer/vulkan.h"
#include "utils.h"
#include "window.h"
bool first_camera_rotate = true;

vec3 init_front;

float camera_height_screen;
float camera_width_screen;
versor camera_rotation;

bool move_camera_input;

float camera_rotate_angle;

void camera_init(Camera* camera){
    camera_width_screen = WINDOW_WIDTH;
    camera_height_screen = WINDOW_HEIGHT;

    camera_rotate_angle = 0;

    glm_mat4_identity(camera->view);
    glm_mat4_identity(camera->projection);

    init_vec3(1.0f, 0 ,  0.0f, camera->front);
    init_vec3(0.0f, 0.0f,  1.0f, camera->up);
    init_vec3(0,0,0, camera->position);

    vec3 look_pos;
    glm_vec3_add(camera->position, camera->front, look_pos);

    glm_lookat(camera->position, look_pos, camera->front , camera->view);

    glm_perspective(45.f, camera_width_screen / camera_height_screen, 0.001f,
                    5000.f, camera->projection);


    camera->projection[1][1] *= -1;//In Vulkan the Z is up down
}

void camera_set_position(Camera* camera, vec3 position){

  init_vec3(position[0], position[1], position[2], camera->position);
  camera_update(camera);

}

void camera_update(Camera* camera){
    vec3 look_pos;
    glm_vec3_add(camera->position, camera->front, look_pos);

    glm_lookat(camera->position, look_pos, camera->up , camera->view);
}

void pe_camera_look_at(Camera* camera, vec3 position){
    glm_lookat(camera->position, position, camera->up, camera->view); 
}

void camera_rotate_control(float yaw, float pitch){
    vec3 front;

    front[0] = cos(glm_rad(yaw)) * cos(glm_rad(pitch));
    front[1] = sin(glm_rad(pitch));
    front[2] = sin(glm_rad(yaw)) * cos(glm_rad(pitch));

    if(first_camera_rotate == true){
        glm_vec3_copy(main_camera.front,init_front);
        glm_vec3_mul(init_front,(vec3){0,5,0},init_front);
        first_camera_rotate = false;
    }
 
    glm_normalize(front);

    glm_vec3_copy(front, main_camera.front);

}

void camera_update_aspect_ratio(Camera* camera){
  glm_perspective(45.f, camera_width_screen / camera_height_screen, 0.001f,
                  5000.f, camera->projection);
}
