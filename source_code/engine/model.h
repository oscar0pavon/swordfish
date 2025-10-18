#ifndef MODEL_H
#define MODEL_H



//#include "images.h"

#include "array.h"

#include "../renderer/cglm//vec3.h"


#include <vulkan/vulkan_core.h>


typedef struct PMesh{
  Array vertex_array;
  Array index_array;

  VkBuffer vertex_buffer;
  VkBuffer index_buffer;
}PMesh;

typedef struct PModel{
    int id;
    unsigned short int texture_count;
    
    Array vertex_array;
    Array index_array;
   
    vec3 min;
    vec3 max;
    

    mat4 model_mat;

    // PTexture texture;
    // PTexture textures[4];

    // PMaterial material;

    VkBuffer vertex_buffer;
    VkBuffer index_buffer;

    Array uniform_buffers;
    Array uniform_buffers_memory;
    Array descriptor_sets;
    VkDescriptorPool descriptor_pool;
 
    vec3 position;
    PMesh mesh;
	  bool gpu_ready;
}PModel;

typedef struct DrawData{
    u32 shader;
    u32 texture;
    u32 vertex;
    u32 index;
}DrawData;

PModel* test_model;
PModel* test_model2;

PModel* pe_vk_model_load(char* path);

int pe_loader_model(const char* path);

int pe_data_loader_models_loaded_count;

#endif // !MODEL_H
