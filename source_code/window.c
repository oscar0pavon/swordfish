#include "window.h"
#include <X11/Xlib.h>

#define WINDOW_WIDTH 1240
#define WINDOW_HEIGHT 720

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


    XSetStandardProperties(display, swordfish_window, 
            "swordfish", "swordfish", None, NULL, 0, NULL);


   
    //handle close the window
    atom_close_window = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, swordfish_window, &atom_close_window, 1);


    XSelectInput(display, swordfish_window, ButtonPressMask | ButtonReleaseMask | FocusChangeMask);


    
    //show the window
    XMapWindow(display, swordfish_window);

}
