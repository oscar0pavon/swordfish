#include "input.h"

#include "window.h"
#include <engine/time.h>

void* handle_input(void* none){

  while (swordfish_running) {

    start_input_time();

    //if (XPending(display)) {
    if (1) {

      XNextEvent(display, &window_event);

      switch (window_event.type) {
      case ClientMessage:

        if (window_event.xclient.message_type ==
                XInternAtom(display, "WM_PROTOCOLS", False) &&
            (Atom)window_event.xclient.data.l[0] ==
                XInternAtom(display, "WM_DELETE_WINDOW", False)) {
          // or prompt the user for confirmation

          swordfish_running = false;

        }

        break;
      case ButtonPress:
        mouse_click_x = window_event.xbutton.x;
        mouse_click_y = window_event.xbutton.y;


        break;

      case ButtonRelease:

        break;

      case FocusIn:

        break;

      case FocusOut:

        break;
      }
    }
  }

  delay_input_time();

}

