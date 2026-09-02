#include "mouse.h"

#include <libinput.h>

#include "input.h"
#include "outputs.h"

double cursor_x, cursor_y;

//the cursor belongs to the virtual desktop the outputs tile into, so it stops
//at its edges rather than wherever the mouse kept going. y clamps to whatever
//output the cursor's x is currently over, since outputs are not necessarily
//the same height
static void clamp_cursor(void) {

  if (cursor_x < 0)
    cursor_x = 0;

  if (cursor_y < 0)
    cursor_y = 0;

  int32_t max_x = sword_virtual_width() - 1;
  if (cursor_x > max_x)
    cursor_x = max_x;

  SwordOutput *out = sword_output_at(cursor_x);
  int32_t max_y = (out ? out->height : 0) - 1;
  if (cursor_y > max_y)
    cursor_y = max_y;
}

void move_cursor_to(double x, double y) {

  cursor_x = x;
  cursor_y = y;

  clamp_cursor();

  send_wayland_pointer_motion(cursor_x, cursor_y);
}

//INFO no rotation is applied here, and adding one is a mistake. cursor_x/y
//live in the virtual desktop's logical space - the same space the layout
//assigns tiles in and draw_surface() places quads in - and a rotated output
//is turned from logical into physical pixels only at draw time, by
//sword_draw_rotated() (sword.c). So logical +x is visually rightward and
//logical +y visually downward on every output, rotated or not, and a
//relative mouse delta already arrives in exactly those terms. Rotating it
//here as well applies the rotation twice and comes out as horizontal mouse
//motion moving the cursor vertically on the rotated output
void move_cursor_by(double dx, double dy) {
  move_cursor_to(cursor_x + dx, cursor_y + dy);
}

void handle_pointer_button(uint32_t button, bool pressed) {
  send_wayland_pointer_button(button, pressed);
}

void handle_pointer_axis(double value) {
  send_wayland_pointer_axis(value);
}

void handle_libinput_pointer_event(InputEvent *event) {

  struct libinput_event_pointer *pointer =
      libinput_event_get_pointer_event(event);

  switch (libinput_event_get_type(event)) {

  case LIBINPUT_EVENT_POINTER_MOTION:
    move_cursor_by(libinput_event_pointer_get_dx(pointer),
                   libinput_event_pointer_get_dy(pointer));
    break;

  //tablets and touchscreens report where the cursor is rather than how far it
  //moved, in a space of their own that libinput scales to whatever is asked.
  //there is no one right height across outputs of different sizes, so this
  //scales against the tallest one
  //TODO unlike a relative delta (see move_cursor_by() above), an absolute
  //position does need the rotation: a touchscreen is physically bolted to its
  //panel, so a touch at the panel's own top-left is not the logical top-left
  //of a rotated output. Left undone because it needs to know which output the
  //device belongs to, and sword_virtual_width()/sword_max_output_height() are
  //global across every output
  case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
    move_cursor_to(
        libinput_event_pointer_get_absolute_x_transformed(
            pointer, sword_virtual_width()),
        libinput_event_pointer_get_absolute_y_transformed(
            pointer, sword_max_output_height()));
    break;

  case LIBINPUT_EVENT_POINTER_BUTTON:
    handle_pointer_button(libinput_event_pointer_get_button(pointer),
                          libinput_event_pointer_get_button_state(pointer) ==
                              LIBINPUT_BUTTON_STATE_PRESSED);
    break;

  //LIBINPUT_EVENT_POINTER_AXIS is the older shape of these three and libinput
  //sends both for every scroll, so taking only the typed ones is what counts
  //a wheel click once instead of twice
  case LIBINPUT_EVENT_POINTER_SCROLL_WHEEL:
  case LIBINPUT_EVENT_POINTER_SCROLL_FINGER:
  case LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS:
    handle_pointer_axis(libinput_event_pointer_get_scroll_value(
        pointer, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL));
    break;

  default:
    break;
  }
}
