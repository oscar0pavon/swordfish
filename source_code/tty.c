// tty = TeleType

#include "tty.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <signal.h>
#include <termios.h>
#include "log.h"
#include <errno.h>
#include <string.h>
#include <xf86drm.h>

#include "device_input.h"

int tty_file = -1;
struct vt_stat virtual_terminal_stat;
struct termios original_termios;
int original_tty_number;

//the console keyboard mode we found the VT in, so it can be put back. K_XLATE
//on a text VT, and worth saving rather than assuming: a VT left by another
//program may be in any of them
static int original_keyboard_mode = K_UNICODE;
static bool keyboard_silenced;

//INFO there is no libseat here on purpose. seatd exists to hand a DRM and
//input fds to a compositor that is *not* root, and to arbitrate between the
//sessions of several users - neither of which is a problem on a single user
//machine where sword runs as root and can open /dev/dri/card0 itself.
//what libseat also did, and what is not about multiple users at all, is tell
//a compositor when its VT went away so it can let go of the display: one user
//still has more than one VT. that half is the kernel's own VT_PROCESS
//protocol, and it is right here
static int drm_fd = -1;
static struct vt_mode original_vt_mode;
static bool vt_mode_taken;
static bool session_active = true;

//the kernel raises these on this process when the VT is taken away and given
//back. any two unused signals would do; these are the ones every VT switching
//program has used since long before logind
#define TTY_RELEASE_SIGNAL SIGUSR1
#define TTY_ACQUIRE_SIGNAL SIGUSR2

//written by a signal handler, read by the compositor loop. the handlers do
//nothing else: drmDropMaster() and libinput_suspend() are not
//async-signal-safe, and the loop comes round every frame anyway
static volatile sig_atomic_t release_pending;
static volatile sig_atomic_t acquire_pending;

static void handle_release_signal(int signal_number) {
  release_pending = 1;
}

static void handle_acquire_signal(int signal_number) {
  acquire_pending = 1;
}

void tty_save_state() {
  char* tty_name;

  tty_name = ttyname(STDIN_FILENO);

  //stdin is not always the console: on the DRM path log_redirect_stdio() has
  //already pointed it at the log file, and openvt hands the VT over on fd 0
  //only some of the time. /dev/tty is this process's controlling terminal
  //whatever happened to its standard descriptors
  if (!tty_name)
    tty_name = "/dev/tty";

  log_info("Saving %s", tty_name);

  tty_file = open(tty_name, O_RDWR);
  if (tty_file < 0) {
    log_error("Failed to open TTY: %s", strerror(errno));
    return;
  }

  if (ioctl(tty_file, VT_GETSTATE, &virtual_terminal_stat) < 0) {
    log_error("Failed to get TTY state: %s", strerror(errno));
    close(tty_file);
    tty_file = -1;
    return;
  }

  original_tty_number = virtual_terminal_stat.v_active;

  if (tcgetattr(tty_file, &original_termios) < 0) {
    log_error("Failed to get original terminal attributes: %s", strerror(errno));
  }

  if (ioctl(tty_file, KDGKBMODE, &original_keyboard_mode) < 0) {
    log_error("Failed to get the console keyboard mode: %s", strerror(errno));
  }
}

//INFO KD_GRAPHICS stops the console *drawing*; it does not stop it reading.
//the kernel keyboard driver goes on translating every key on this VT into
//characters for whoever holds the tty, so everything typed into a client is
//also echoed onto the console under the frames we present, and ctrl+c is
//still turned into a SIGINT on sword's own process group - which is what
//closes the compositor when a client was the one being typed into. libinput
//reads the same keys from /dev/input/event* directly, so the console's copy
//is pure duplication. K_OFF makes the driver deliver nothing at all and
//leaves evdev the only reader of the keyboard
static void tty_silence_keyboard(void) {

  if (tty_file < 0)
    return;

  //KDSKBMUTE, the ioctl libseat reaches for first, is not in linux/kd.h at all
  //- it is a define seatd carries itself for a kernel that does not implement
  //it either. K_OFF is the one the console driver actually answers
  if (ioctl(tty_file, KDSKBMODE, K_OFF) < 0) {
    log_error("Can't silence the console keyboard: %s", strerror(errno));
    return;
  }

  keyboard_silenced = true;

  log_info("Console keyboard is off, input comes from evdev only");
}

//INFO the console is unusable until this runs - no key typed on it produces
//anything - so every way out of sword has to reach it. that is why
//tty_session_init() hands it to atexit() and to the crash handler as well as
//close_sword() calling tty_session_finish()
static void tty_restore_keyboard(void) {

  if (!keyboard_silenced)
    return;

  if (ioctl(tty_file, KDSKBMODE, original_keyboard_mode) < 0)
    log_error("Failed to restore the console keyboard mode: %s", strerror(errno));

  keyboard_silenced = false;
}

void tty_set_to_graphics(){

  if (tty_file < 0)
    return;

  //without this the kernel console goes on drawing its cursor and whatever is
  //echoed to it into the same scanout we are presenting to
  if (ioctl(tty_file, KDSETMODE, KD_GRAPHICS) < 0) {
        log_error("KD_GRAPHICS failed: %s", strerror(errno));
  }
}

void tty_restore_state() {

  if (tty_file < 0)
    return;

  //hand VT switching back to the kernel before anything else: if we exit with
  //VT_PROCESS still set and no one left to answer the release signal, the
  //machine cannot switch VT at all any more
  if (vt_mode_taken) {
    if (ioctl(tty_file, VT_SETMODE, &original_vt_mode) < 0)
      log_error("Failed to restore VT mode: %s", strerror(errno));
    vt_mode_taken = false;
  }

  tty_restore_keyboard();

  // Set tty back to text mode
  if (ioctl(tty_file, KDSETMODE, KD_TEXT) < 0) {
    log_error("Failed to set TTY to text mode: %s", strerror(errno));
  }

  if (ioctl(tty_file, VT_ACTIVATE, original_tty_number) < 0) {
    log_error("Failed to activate original TTY: %s", strerror(errno));
  }

  if (tcsetattr(tty_file, TCSAFLUSH, &original_termios) < 0) {
    log_error("Failed to restore original terminal attributes: %s", strerror(errno));
  }

  close(tty_file);
  tty_file = -1;
}

int tty_drm_fd(void) {
  return drm_fd;
}

bool tty_session_is_active(void) {
  return session_active;
}

//INFO master has to be taken *before* vulkan is initialised. mesa's wsi_display
//keeps the primary node fd radv opened for itself and only uses it if that fd
//is master (wsi_display_init_wsi), which is what happens when sword is the
//first thing to touch card0 - and then the fd doing the scanout belongs to
//mesa and nothing here can hand the display back on a VT switch. holding
//master first makes radv's own fd non-master, so wsi keeps fd -1 and
//vkAcquireDrmDisplayEXT installs *this* one instead
static bool take_drm_master(const char *gpu_path) {

  drm_fd = open(gpu_path, O_RDWR | O_CLOEXEC);
  if (drm_fd < 0) {
    log_error("Can't open %s: %s", gpu_path, strerror(errno));
    return false;
  }

  //opening the primary node while nothing else holds master already makes us
  //master; this is for the case where a compositor just let go of it
  if (drmSetMaster(drm_fd) < 0) {
    log_error("Can't become DRM master on %s: %s", gpu_path, strerror(errno));
    close(drm_fd);
    drm_fd = -1;
    return false;
  }

  log_info("DRM master on %s (fd %i)", gpu_path, drm_fd);
  return true;
}

bool tty_session_init(const char *gpu_path) {

  tty_save_state();

  if (tty_file < 0)
    return false;

  if (!take_drm_master(gpu_path))
    return false;

  tty_set_to_graphics();
  tty_silence_keyboard();

  //super+q calls exit() and a fatal signal never comes back here at all, so
  //the restore cannot live on the one path close_sword() takes. both of
  //these end in tty_restore_state(), which is a no-op the second time
  atexit(tty_session_finish);
  log_crash_hook = tty_emergency_restore;

  if (ioctl(tty_file, VT_GETMODE, &original_vt_mode) < 0) {
    log_error("Failed to read VT mode: %s", strerror(errno));
    return false;
  }

  struct sigaction release_action = {.sa_handler = handle_release_signal};
  struct sigaction acquire_action = {.sa_handler = handle_acquire_signal};
  sigemptyset(&release_action.sa_mask);
  sigemptyset(&acquire_action.sa_mask);
  sigaction(TTY_RELEASE_SIGNAL, &release_action, NULL);
  sigaction(TTY_ACQUIRE_SIGNAL, &acquire_action, NULL);

  //VT_PROCESS: the kernel stops switching VTs by itself and asks us first.
  //nothing moves until VT_RELDISP answers, which is the whole point - it is
  //the window in which the display is given back
  struct vt_mode mode = {
      .mode = VT_PROCESS,
      .waitv = 0,
      .relsig = TTY_RELEASE_SIGNAL,
      .acqsig = TTY_ACQUIRE_SIGNAL,
  };

  if (ioctl(tty_file, VT_SETMODE, &mode) < 0) {
    log_error("Failed to take over VT switching: %s", strerror(errno));
    return false;
  }

  vt_mode_taken = true;
  session_active = true;

  log_info("VT session on tty%i, switching is ours", original_tty_number);

  return true;
}

void tty_session_finish(void) {

  tty_restore_state();

  if (drm_fd >= 0) {
    drmDropMaster(drm_fd);
    close(drm_fd);
    drm_fd = -1;
  }
}

//INFO ioctl and nothing else: this runs from the crash handler on a process
//that is already dying. it leaves the fds open and the bookkeeping alone -
//all it owes the machine is a console that can be typed on again
void tty_emergency_restore(void) {

  if (tty_file < 0)
    return;

  if (vt_mode_taken)
    ioctl(tty_file, VT_SETMODE, &original_vt_mode);

  if (keyboard_silenced)
    ioctl(tty_file, KDSKBMODE, original_keyboard_mode);

  ioctl(tty_file, KDSETMODE, KD_TEXT);
}

//INFO evdev fds are not scoped to a VT: libinput holds /dev/input/event* open
//directly, so without this every key typed into whatever compositor took the
//VT from us is *also* delivered to our clients. revoking those fds is the
//other thing seatd would have done
static void session_deactivate(void) {

  log_info("VT released, letting go of the display");

  if (libinput)
    libinput_suspend(libinput);

  if (drm_fd >= 0 && drmDropMaster(drm_fd) < 0)
    log_warn("Can't drop DRM master: %s", strerror(errno));

  session_active = false;

  //only now may the kernel complete the switch. a 1 means we agree to go; a 0
  //would refuse it and keep the VT
  if (ioctl(tty_file, VT_RELDISP, 1) < 0)
    log_error("VT_RELDISP failed: %s", strerror(errno));
}

static void session_activate(void) {

  log_info("VT acquired, taking the display back");

  if (ioctl(tty_file, VT_RELDISP, VT_ACKACQ) < 0)
    log_error("VT_RELDISP(VT_ACKACQ) failed: %s", strerror(errno));

  if (drm_fd >= 0 && drmSetMaster(drm_fd) < 0)
    log_warn("Can't take DRM master back: %s", strerror(errno));

  if (libinput)
    libinput_resume(libinput);

  session_active = true;
}

void tty_session_handle_pending(void) {

  if (tty_file < 0)
    return;

  if (release_pending) {
    release_pending = 0;
    session_deactivate();
  }

  if (acquire_pending) {
    acquire_pending = 0;
    session_activate();
  }
}

void tty_switch_to(int vt_number) {

  if (tty_file < 0) {
    log_warn("No tty to switch from");
    return;
  }

  //this only asks. the kernel answers by raising the release signal on us,
  //and the switch happens in session_deactivate() above
  if (ioctl(tty_file, VT_ACTIVATE, vt_number) < 0)
    log_error("Can't switch to VT %i: %s", vt_number, strerror(errno));
}
