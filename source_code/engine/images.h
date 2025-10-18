#ifndef IMAGES_H
#define IMAGES_H

#include <GLES2/gl2.h>

#include "numbers.h"

typedef struct PImage{
  unsigned short int width;
  unsigned short int heigth;
  unsigned char* pixels_data;
}PImage;

typedef struct PTexture{
    PImage image;
    unsigned int id;
		int format;
		bool gpu_loaded;
}PTexture;

int pe_load_image(const char* path, PImage* image);
int pe_load_image_with_format(const char* path, GLint format, PImage* out_image);
void free_image(PImage*);

int pe_load_texture(const char* path, PTexture*);

int texture_load_from_memory(PTexture* texture,u32 size,void* data);

int image_load_from_memory(PImage* image,void* data, u32 size);

void pe_gpu_load_texture(PTexture* texture);

#endif
