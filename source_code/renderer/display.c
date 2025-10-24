#include "display.h"

#include "vulkan.h"
#include <stdio.h>

void vk_get_displays() {
  u32 display_count;

  vkGetPhysicalDeviceDisplayPropertiesKHR(vk_physical_device, &display_count,
                                          NULL);

  printf("Vulkan displays count: %i\n", display_count);
}
