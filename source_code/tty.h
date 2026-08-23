#ifndef PTTY_H
#define PTTY_H

#include <stdbool.h>

void tty_save_state();
void tty_restore_state();
void tty_set_to_graphics();

//the whole VT session on the bare DRM path: takes the tty, becomes DRM master
//and asks the kernel to hand VT switches to us rather than doing them behind
//our back. libseat did this before, through seatd - see tty.c for why a
//single user machine does not need a seat daemon to do it
bool tty_session_init(const char *gpu_path);

void tty_session_finish(void);

//acts on a VT switch the signal handler recorded. called once per turn of the
//compositor loop, since dropping DRM master and suspending libinput are not
//things a signal handler may do
void tty_session_handle_pending(void);

bool tty_session_is_active(void);

//the fd swordfish holds DRM master on, handed to vulkan so mesa scans out
//through it and this file can take it away again. -1 when there is none
int tty_drm_fd(void);

void tty_switch_to(int vt_number);

#endif
