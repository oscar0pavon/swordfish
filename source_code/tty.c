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
#include <xf86drmMode.h>

#include "device_input.h"
#include "input.h"
#include "outputs.h"

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

#define SAVED_CRTC_MAX 8
#define SAVED_CRTC_MAX_CONNECTORS 8

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

//INFO the mode and framebuffer fbcon left each crtc on, put back in
//tty_session_finish(). Dropping master and exiting does not re-light a
//monitor by itself
typedef struct SavedCrtc {
  uint32_t crtc_id;
  uint32_t buffer_id;
  uint32_t x, y;
  drmModeModeInfo mode;
  int mode_valid;
  uint32_t connectors[SAVED_CRTC_MAX_CONNECTORS];
  int connector_count;
} SavedCrtc;

static SavedCrtc saved_crtcs[SAVED_CRTC_MAX];
static int saved_crtcs_count;

static void saved_crtc_collect_connectors(drmModeRes *resources,
                                          SavedCrtc *saved) {

  saved->connector_count = 0;

  for (int i = 0; i < resources->count_connectors &&
                  saved->connector_count < SAVED_CRTC_MAX_CONNECTORS;
       i++) {

    drmModeConnector *connector =
        drmModeGetConnector(drm_fd, resources->connectors[i]);
    if (!connector)
      continue;

    if (connector->encoder_id) {
      drmModeEncoder *encoder = drmModeGetEncoder(drm_fd, connector->encoder_id);
      if (encoder) {
        if (encoder->crtc_id == saved->crtc_id)
          saved->connectors[saved->connector_count++] = connector->connector_id;
        drmModeFreeEncoder(encoder);
      }
    }

    drmModeFreeConnector(connector);
  }
}

static void save_crtc_state(void) {

  saved_crtcs_count = 0;

  if (drm_fd < 0)
    return;

  drmModeRes *resources = drmModeGetResources(drm_fd);
  if (!resources) {
    log_warn("Can't enumerate CRTCs to save the console mode: %s",
             strerror(errno));
    return;
  }

  for (int i = 0;
       i < resources->count_crtcs && saved_crtcs_count < SAVED_CRTC_MAX; i++) {

    drmModeCrtc *crtc = drmModeGetCrtc(drm_fd, resources->crtcs[i]);
    if (!crtc)
      continue;

    SavedCrtc *saved = &saved_crtcs[saved_crtcs_count++];

    saved->crtc_id = crtc->crtc_id;
    saved->buffer_id = crtc->buffer_id;
    saved->x = crtc->x;
    saved->y = crtc->y;
    saved->mode = crtc->mode;
    saved->mode_valid = crtc->mode_valid && crtc->buffer_id;

    if (saved->mode_valid)
      saved_crtc_collect_connectors(resources, saved);
    else
      saved->connector_count = 0;

    log_info("Saved crtc %u: %s, fb %u, %u connector(s)", saved->crtc_id,
             saved->mode_valid ? saved->mode.name : "off", saved->buffer_id,
             saved->connector_count);

    drmModeFreeCrtc(crtc);
  }

  drmModeFreeResources(resources);
}

//INFO only while we still hold master: every modeset after drmDropMaster()
//is EACCES
static void restore_crtc_state(void) {

  if (drm_fd < 0)
    return;

  for (int i = 0; i < saved_crtcs_count; i++) {

    SavedCrtc *saved = &saved_crtcs[i];

    //off rather than left on a swapchain framebuffer about to be freed - that
    //dangling scanout is what reads as "no signal"
    if (!saved->mode_valid || saved->connector_count == 0) {
      if (drmModeSetCrtc(drm_fd, saved->crtc_id, 0, 0, 0, NULL, 0, NULL) < 0)
        log_warn("Can't turn crtc %u off: %s", saved->crtc_id, strerror(errno));
      continue;
    }

    if (drmModeSetCrtc(drm_fd, saved->crtc_id, saved->buffer_id, saved->x,
                       saved->y, saved->connectors, saved->connector_count,
                       &saved->mode) < 0)
      log_warn("Can't restore crtc %u to the console mode: %s", saved->crtc_id,
               strerror(errno));
    else
      log_info("Restored crtc %u to %s", saved->crtc_id, saved->mode.name);
  }
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

//INFO whoever was DRM master before us programmed the CRTC's cursor plane and
//never turned it off on the way out - dropping master changes nothing about
//the plane state the kernel holds. we scan out through vulkan's display
//swapchain, which flips the *primary* plane and leaves every other plane
//alone, so sway's cursor bitmap goes on being composited over our frames,
//frozen where it was left, and there are two arrows on screen: theirs and the
//one cursor.c draws. clearing it belongs everywhere we take master
static void drm_disable_cursor_planes(void) {

  if (drm_fd < 0)
    return;

  drmModeRes *resources = drmModeGetResources(drm_fd);
  if (!resources) {
    log_warn("Can't enumerate CRTCs to clear the cursor plane: %s",
             strerror(errno));
    return;
  }

  //a handle of 0 is what turns the plane off, and the legacy cursor ioctl is
  //translated onto the universal cursor plane by the kernel, so this reaches
  //an atomic client's cursor as well as a legacy one's
  for (int i = 0; i < resources->count_crtcs; i++)
    if (drmModeSetCursor(drm_fd, resources->crtcs[i], 0, 0, 0) < 0)
      log_debug("No cursor plane cleared on CRTC %u: %s", resources->crtcs[i],
                strerror(errno));

  drmModeFreeResources(resources);
}

bool tty_session_init(const char *gpu_path) {

  tty_save_state();

  if (tty_file < 0)
    return false;

  if (!take_drm_master(gpu_path))
    return false;

  //before vulkan modesets anything, so this records the console's own state
  save_crtc_state();

  //sword may equally be started from a VT another compositor just left
  drm_disable_cursor_planes();

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

  //the order matters: the modeset needs master, and KD_TEXT after it so fbcon
  //draws onto a framebuffer already scanning out
  restore_crtc_state();

  if (drm_fd >= 0) {
    drmDropMaster(drm_fd);
    close(drm_fd);
    drm_fd = -1;
  }

  tty_restore_state();
}

//INFO ioctl and nothing else: this runs from the crash handler on a process
//that is already dying. it leaves the fds open and the bookkeeping alone -
//all it owes the machine is a console that can be typed on again
void tty_emergency_restore(void) {

  if (tty_file < 0)
    return;

  //drmModeSetCrtc is an ioctl and a stack struct, nothing more
  restore_crtc_state();

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

  //logged before we let go, so it is a known-good baseline to compare
  //session_activate()'s post-switch log against
  sword_log_display_routing("before release");

  //while we still have the keyboard: the release of ctrl+alt+Fn goes to the VT
  //taking over, so the clients here have to be told by hand
  input_release_pressed_keys();

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

  //whoever was DRM master while we were not (sway, on another VT) may have
  //put a different connector on the crtc each render target's plane is
  //permanently wired to - put it back before sword's next present, or that
  //plane goes on scanning out to whichever monitor is on its crtc now, not
  //the one it was chosen for. see outputs.c for the whole mechanism
  sword_restore_display_routing();

  //the compositor that had the VT left its hardware cursor on the plane
  drm_disable_cursor_planes();

  //logged after the restore above, so a mismatch here means the restore
  //itself did not work rather than the switch having moved anything
  sword_log_display_routing("after acquire");

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
