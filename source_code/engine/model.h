#ifndef MODEL_H
#define MODEL_H



//#include "images.h"

#include "array.h"

#include "../renderer/cglm/vec3.h"


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

static int pe_data_loader_models_loaded_count;

PModel* pe_vk_model_load(PModel* model, char* path);


int pe_load_model(PModel* model, const char *path);

#endif // !MODEL_H
