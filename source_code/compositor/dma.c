#include "dma.h"
#include <complex.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <wayland-server.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <engine/images.h>
#include "feedback.h"
#include "swordfish.h"
#include "linux-dmabuf.h"
#include "compositor.h"

#include <engine/renderer/vk_images.h>
#include "client_buffer.h"
#include "drm_format.h"
#include "retire.h"

#include <xf86drm.h>

uint64_t main_device_id;


static struct zwp_linux_dmabuf_v1_interface dmabuf_data;
      

uint64_t get_drm_device_id(const char *device_path);

//clients must be handed the *render* node, never the primary one. on the tty
//swordfish is DRM master on card0, so a client that opens it cannot
//authenticate and mesa ends up with no device at all: eglChooseConfig returns
//nothing, egl_config stays garbage and eglCreateWindowSurface fails. the node
//permissions say the same thing - card0 is root:video, renderD128 is world
//readable because it is the one clients are meant to use
uint64_t get_render_device_id(const char *primary_path) {
  int fd = open(primary_path, O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    perror("open primary node");
    return 0;
  }

  drmDevicePtr device;
  if (drmGetDevice2(fd, 0, &device) != 0) {
    printf("Can't get the drm device for %s\n", primary_path);
    close(fd);
    return 0;
  }

  uint64_t device_id = 0;

  if (device->available_nodes & (1 << DRM_NODE_RENDER)) {
    printf("Advertising render node %s\n", device->nodes[DRM_NODE_RENDER]);
    device_id = get_drm_device_id(device->nodes[DRM_NODE_RENDER]);
  } else {
    printf("No render node for %s, falling back to it\n", primary_path);
    device_id = get_drm_device_id(primary_path);
  }

  drmFreeDevice(&device);
  close(fd);

  return device_id;
}

uint64_t get_drm_device_id(const char *device_path) {
    struct stat st;
    if (stat(device_path, &st) < 0) {
        perror("stat device_path");
        return 0; // Error
    }
    // The device ID is a combination of major and minor numbers
    return (uint64_t)st.st_rdev; 
}



void params_add(WClient *client,
		    WResource *resource,
		    int32_t fd,
		    uint32_t plane_idx,
		    uint32_t offset,
		    uint32_t stride,
		    uint32_t modifier_hi,
        uint32_t modifier_lo){

  printf("Adding params\n");

  DMAParams *params = wl_resource_get_user_data(resource);

  if (!params) {
    close(fd);
    return;
  }

  if (plane_idx >= MAX_DMA_PLANES) {
    close(fd);
    return;
  }

  if (params->fds[plane_idx] != -1) {
    close(fd);
    wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX,
                           "Plane already added");
    return;
  }
  if (fd == -1 || fcntl(fd, F_GETFL) == -1) {
    perror("Received an invalid or closed FD from client");
    wl_resource_post_error(resource,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                           "Received invalid FD");
    return;
  }
  params->fds[plane_idx] = fd;
  params->offsets[plane_idx] = offset;
  params->strides[plane_idx] = stride;
  // Reconstruct the 64-bit modifier
  params->modifiers[plane_idx] = ((uint64_t)modifier_hi << 32) | modifier_lo;
  params->num_planes++;

  printf("Added DMA buffer plane %u with FD %d\n", plane_idx, fd);
}

static void buffer_destroy(WClient *client, WResource *resource) {
  wl_resource_destroy(resource);
}

struct wl_buffer_interface buffer_implementation = {
  .destroy = buffer_destroy
};

//the resource destructor rather than only the destroy request handler, so it
//also runs when the client disconnects without asking - the request-only
//version leaked every imported image a dying client left behind. the image
//goes through the retire list, not pe_vk_clean_image(): this is the compositor
//thread, and the render thread can have the image recorded into a frame that
//is still in flight - destroying it where the request arrives is what the
//validation layer reported as destroying a view still in use by a descriptor
//set
static void destroy_buffer_resource(WResource *resource) {

  ClientBuffer *buffer = wl_resource_get_user_data(resource);

  if (!buffer)
    return;

  printf("#### Retiring image size %i %i %p\n", buffer->texture.width,
         buffer->texture.heigth, buffer);

  retire_texture(&buffer->texture);

  free(buffer);
}

//the import shared by create_immed and create. on success the plane fds have
//been consumed - plane 0 belongs to vulkan now, the rest are closed - and the
//returned wl_buffer carries the ClientBuffer surface_attach() reads. on
//failure the caller answers, because the two requests answer differently:
//create_immed with a protocol error, create with the failed event
static WResource *params_import(WClient *client, WResource *resource,
                                uint32_t buffer_id, int32_t width,
                                int32_t height, uint32_t format) {

  DMAParams *buffer = wl_resource_get_user_data(resource);

  //a params object makes one buffer and is done - the protocol calls a second
  //create on the same params ALREADY_USED
  if (buffer->used) {
    wl_resource_post_error(resource,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED,
                           "params already used to create a buffer");
    return NULL;
  }

  buffer->width = width;
  buffer->height = height;
  buffer->format = format;

  //the client's format was thrown away here and every buffer imported as
  //B8G8R8A8_UNORM. that is the right byte order for XR24 and the wrong one for
  //half the list, and UNORM is wrong for all of them - see drm_format.c
  const DrmFormat *drm_format = drm_format_find(format);

  if (!drm_format) {
    printf("Client asked for format 0x%08x, which has no vulkan equivalent\n",
           format);
    wl_resource_post_error(resource,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT,
                           "unsupported format");
    return NULL;
  }

  if (buffer->num_planes == 0) {
    wl_resource_post_error(resource,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE,
                           "no planes added");
    return NULL;
  }

  printf("Creatig buffer with %i %i format %s modifier 0x%016lx planes %i "
         "stride %u offset %u\n",
         width, height, drm_format->name,
         (unsigned long)buffer->modifiers[0], buffer->num_planes,
         buffer->strides[0], buffer->offsets[0]);

  //the same struct shared_memory.c makes, tagged so surface_attach() can tell
  //a zero-copy import from a buffer it has to upload itself
  ClientBuffer *new_buffer = calloc(1, sizeof(ClientBuffer));

  new_buffer->type = CLIENT_BUFFER_DMA;
  new_buffer->texture.width = width;
  new_buffer->texture.heigth = height;

  PImageImportInfo import = {
      .width = width,
      .height = height,
      .format = drm_format->vulkan,
      .modifier = buffer->modifiers[0],
      .plane_count = buffer->num_planes,
      .file_descriptor = buffer->fds[0],
      .opaque = drm_format->opaque,
  };

  for (int i = 0; i < buffer->num_planes && i < PE_MAX_IMAGE_PLANES; i++) {
    import.offsets[i] = buffer->offsets[i];
    import.strides[i] = buffer->strides[i];
  }

  if (!pe_vk_import_image(&new_buffer->texture, &import)) {
    free(new_buffer);
    //the fds are still the params' own: an import that did not happen consumed
    //nothing, and destroy_params closes them
    return NULL;
  }

  buffer->used = true;

  //vulkan owns plane 0's fd from the moment the import succeeds - closing it
  //here closed an fd the driver was still using, and destroy_params closed the
  //same number a second time after that, onto whatever file it had been reused
  //for by then. the extra planes were never handed to anyone, so they do close;
  //everything is marked consumed so destroy_params has nothing left to touch
  for (int i = 0; i < buffer->num_planes; i++) {
    if (i > 0 && buffer->fds[i] != -1)
      close(buffer->fds[i]);
    buffer->fds[i] = -1;
  }

  //buffer_id 0 is the non-immediate path asking libwayland for a server
  //allocated id, which is the object the created event announces
  WResource *buffer_resource =
      wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
  if (!buffer_resource) {
    retire_texture(&new_buffer->texture);
    free(new_buffer);
    wl_client_post_no_memory(client);
    return NULL;
  }

  wl_resource_set_implementation(buffer_resource, &buffer_implementation,
                                 new_buffer, destroy_buffer_resource);

  return buffer_resource;
}

void params_create(struct wl_client *client,
		       struct wl_resource *resource,
		       int32_t width,
		       int32_t height,
		       uint32_t format,
           uint32_t flags){

  //the non-immediate path. it used to be a stub that printed and returned, so
  //a client using it - the protocol's original form - waited forever for a
  //created event that never came
  WResource *buffer_resource =
      params_import(client, resource, 0, width, height, format);

  if (!buffer_resource) {
    //also sent after a protocol error, where the client is already dead and
    //one more event changes nothing
    zwp_linux_buffer_params_v1_send_failed(resource);
    return;
  }

  zwp_linux_buffer_params_v1_send_created(resource, buffer_resource);
}

static void create_immediate(WClient *client,
                               WResource *resource,
                               uint32_t buffer_id,
                               int32_t width, int32_t height,
                               uint32_t format, uint32_t flags) {

  if (!params_import(client, resource, buffer_id, width, height, format)) {
    //a repeat if the parameter checks already posted a more specific one,
    //which libwayland ignores; the import failure itself posts nothing
    wl_resource_post_error(resource,
                           ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER,
                           "could not import buffer");
  }
}

static void destroy_params_resource(WClient *client, WResource *resource) {
    wl_resource_destroy(resource);
    printf("Destroyed params resource\n");
}

static const struct zwp_linux_buffer_params_v1_interface params_implementation = {
    .destroy = destroy_params_resource,
    .add = params_add,
    .create = params_create,
    .create_immed = create_immediate
};

void destroy_params(WResource *resource) {
    DMAParams *params = wl_resource_get_user_data(resource);
    
    if (params) {
        // Close any FDs that might not have been consumed yet (e.g., if creation failed)
        for (int i = 0; i < MAX_DMA_PLANES; i++) {
            if (params->fds[i] != -1) {
                close(params->fds[i]);
            }
        }
        free(params);
    }
    printf("Destroyed params\n");
}

static void create_params(WClient *client, WResource *resource, uint32_t id) {

  printf("Received create_params request. Creating new buffer_params resource ID: %u\n", id);

  WResource *params_resource =
      wl_resource_create(client, &zwp_linux_buffer_params_v1_interface,
                         wl_resource_get_version(resource), id);


  DMAParams *params = calloc(1, sizeof(DMAParams));

  for (int i = 0; i < MAX_DMA_PLANES; i++) {
      params->fds[i] = -1;
  }

  params->client = client;

  wl_resource_set_user_data(params_resource, params);

  wl_resource_set_destructor(params_resource, destroy_params);


  wl_resource_set_implementation(params_resource, &params_implementation,
                                 params, destroy_params);

  printf("Created params\n");
}

static void destroy_dmabuf_resource(struct wl_resource *resource) {
    // You can retrieve and free any user data attached to this resource if necessary
    // void *data = wl_resource_get_user_data(resource);
    // free(data); // If you allocated data specifically for this resource

    printf("Destroying zwp_linux_dmabuf_v1 resource: ID %u\n", wl_resource_get_id(resource));
    // The resource itself is managed by the Wayland library, no need to free 'resource'
}

void destry_dma(struct wl_client *client,
    struct wl_resource *resource){

  printf("destry dma");
}

void bind_dma(WClient *client, void *data, uint32_t version,
                       uint32_t id) {
  
  printf("## Implementing DMA buffers\n");

  WResource *resource;

  resource = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, version, id);
  if (!resource) {
    wl_client_post_no_memory(client);
    printf("Can't implement DMA\n");
    return;
  }
  
  dmabuf_data.destroy = destry_dma;
  dmabuf_data.create_params = create_params;
  dmabuf_data.get_default_feedback = get_feedback;
  dmabuf_data.get_surface_feedback = get_surface_feedback;


  wl_resource_set_implementation(resource, &dmabuf_data, NULL, destroy_dmabuf_resource);

  printf("Bound zwp_linux_dmabuf_v1 global for client (ID %u, Version %u)\n", id, version);
}


void init_dma(){

  printf("Added DMA global\n");

  main_device_id = get_render_device_id("/dev/dri/card0");

  wl_global_create(compositor.display, &zwp_linux_dmabuf_v1_interface, 4,
                   &compositor, bind_dma);

}
