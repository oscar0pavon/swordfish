#include "window.h"
#include <X11/Xlib.h>

#define VK_USE_PLATFORM_XLIB_KHR // Must be defined before including vulkan.h
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>




Display* display;
XEvent window_event;
Window swordfish_window; 

int screen;

int mouse_click_x = 0;
int mouse_click_y = 0;

bool swordfish_running = true;


XSetWindowAttributes window_attributes;

Colormap color_map;



Atom atom_close_window; 

VkInstance instance;
VkSurfaceKHR surface;

void create_vulkan_instance_and_surface() {

    VkApplicationInfo appInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "swordfish",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "swordfish_engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    const char* extensionNames[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
    };

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo,
        .enabledExtensionCount = sizeof(extensionNames) / sizeof(extensionNames[0]),
        .ppEnabledExtensionNames = extensionNames,
    };

    if (vkCreateInstance(&createInfo, NULL, &instance) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan instance!\n");
        exit(1);
    }

    VkXlibSurfaceCreateInfoKHR surfaceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
        .dpy = display,
        .window = swordfish_window,
    };

    if (vkCreateXlibSurfaceKHR(instance, &surfaceCreateInfo, NULL, &surface) != VK_SUCCESS) {
        fprintf(stderr, "Failed to create Vulkan Xlib surface!\n");
        exit(1);
    }
}

void destroy_vulkan_surface_and_instance() {
    vkDestroySurfaceKHR(instance, surface, NULL);
    vkDestroyInstance(instance, NULL);
}

void close_window() {

  destroy_vulkan_surface_and_instance();

  XDestroyWindow(display, swordfish_window);

  XCloseDisplay(display);
}

void create_window(){
    display = XOpenDisplay(NULL); // NULL for default display
    if (display == NULL) {
        // Handle error
    }
    


    screen = DefaultScreen(display);

    window_attributes.colormap = color_map;
    window_attributes.border_pixel = 0;
    window_attributes.event_mask =
        ExposureMask | KeyPressMask | StructureNotifyMask;



    // swordfish_window= XCreateWindow(display, RootWindow(display, window_visual->screen),
    //         0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0, 
    //         window_visual->depth, InputOutput, window_visual->visual, 
    //         CWBorderPixel | CWColormap | CWEventMask, &window_attributes);

    swordfish_window = XCreateSimpleWindow(display, RootWindow(display, screen),
                                 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 1,
                                 BlackPixel(display, screen), WhitePixel(display, screen));


    XClassHint class_hint;
    class_hint.res_name = "swordfish";
    class_hint.res_class = "swordfish"; 
    XSetClassHint(display, swordfish_window, &class_hint);
    XSetStandardProperties(display, swordfish_window, 
            "swordfish", "swordfish", None, NULL, 0, NULL);


   
    //handle close the window
    atom_close_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, swordfish_window, &atom_close_window, 1);


    XSelectInput(display, swordfish_window, ButtonPressMask | ButtonReleaseMask | FocusChangeMask);

    create_vulkan_instance_and_surface();
    
    //show the window
    XMapWindow(display, swordfish_window);

}
