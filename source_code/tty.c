// tty = TeleType

#include "tty.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <termios.h>

int tty_file;
struct vt_stat virtual_terminal_stat;
struct termios original_termios;
int original_tty_number;

void tty_save_state() {
  char* tty_name;

  tty_name = ttyname(STDIN_FILENO);
  printf("Saving %s\n", tty_name);

  tty_file = open(tty_name, O_RDWR);
  if (tty_file < 0) {
    perror("Failed to open TTY");
  }

  if (ioctl(tty_file, VT_GETSTATE, &virtual_terminal_stat) < 0) {
    perror("Failed to get TTY state");
    close(tty_file);
  }

  original_tty_number = virtual_terminal_stat.v_active;

  if (tcgetattr(tty_file, &original_termios) < 0) {
    perror("Failed to get original terminal attributes");
    close(tty_file);
  }


}

void tty_set_to_graphics(){

  if (ioctl(tty_file, KD_GRAPHICS, NULL) < 0) {
        perror("KD_GRAPHICS failed");
  }
}

void tty_restore_state() {


  // Set tty back to text mode
  if (ioctl(tty_file, KD_TEXT, NULL) < 0) {
    perror("Failed to set TTY to text mode");
  }

  if (ioctl(tty_file, VT_ACTIVATE, original_tty_number) < 0) {
    perror("Failed to activate original TTY");
  }

  if (tcsetattr(tty_file, TCSAFLUSH, &original_termios) < 0) {
    perror("Failed to restore original terminal attributes");
  }

  close(tty_file);
}
