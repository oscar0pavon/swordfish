#include "launch.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "log.h"
#include <errno.h>
#include <signal.h>
#include <string.h>

//this used to sit in build.c beside call_make(), which ran `make` and piped
//its output into the 3D scene. the scene is 3dtop's now and the build display
//went with it; spawning a program from a keybinding is the compositor's own
//job and is all that is left
//
//a blocking spawn is exactly what a keybinding must not do: it runs on the
//compositor loop, so waiting there stops pway from being pumped and freezes
//the window until the program is closed. double fork so the grandchild is
//reparented to init and nothing has to be reaped later

//INFO the grandchild is init's now, not ours, so nothing kills it when sword
//exits - no SIGHUP, no process group in common, and a client that never
//notices its wayland socket closed goes on running headless forever. so the
//middle child hands the grandchild's pid back through a pipe and
//launch_close_programs() ends them at shutdown
#define MAX_LAUNCHED_PROGRAMS 64
static pid_t launched_programs[MAX_LAUNCHED_PROGRAMS];
static int launched_program_count;

void launch_program(const char* command){

  int pid_pipe[2];
  if (pipe(pid_pipe) == -1) {
    log_error("pipe: %s", strerror(errno));
    return;
  }

  pid_t pid = fork();
  if (pid == -1) {
    log_error("fork: %s", strerror(errno));
    close(pid_pipe[0]);
    close(pid_pipe[1]);
    return;
  }

  if (pid == 0) {
    close(pid_pipe[0]);

    pid_t program_pid = fork();
    if (program_pid == 0) {
      close(pid_pipe[1]);
      //own session, so the child does not take our controlling terminal. it
      //also makes the child its own process group leader, which is what lets
      //the negative kill below reach whatever it spawns in turn
      setsid();
      execlp(command, command, NULL);
      log_error("execlp: %s", strerror(errno));
      _exit(EXIT_FAILURE);
    }

    if (program_pid > 0)
      (void)write(pid_pipe[1], &program_pid, sizeof(program_pid));

    _exit(program_pid == -1 ? EXIT_FAILURE : EXIT_SUCCESS);
  }

  close(pid_pipe[1]);

  pid_t program_pid = -1;
  ssize_t read_size = read(pid_pipe[0], &program_pid, sizeof(program_pid));
  close(pid_pipe[0]);

  //only the short lived middle child is waited for
  waitpid(pid, NULL, 0);

  if (read_size != sizeof(program_pid)) {
    log_error("Could not launch %s", command);
    return;
  }

  log_info("Launching %s as %d", command, program_pid);

  if (launched_program_count == MAX_LAUNCHED_PROGRAMS) {
    log_warn("Launched program table full, %s will outlive sword", command);
    return;
  }

  launched_programs[launched_program_count++] = program_pid;
}

//the program called setsid(), so its pid is its process group's id as well and
//the negative kill reaches the children it spawned itself - firefox's content
//processes are the case that matters. a program that already exited leaves a
//pid nobody answers for, which is the one errno worth staying quiet about
void launch_close_programs(void){

  for (int i = 0; i < launched_program_count; i++) {
    if (kill(-launched_programs[i], SIGTERM) == -1 && errno != ESRCH)
      log_error("kill %d: %s", launched_programs[i], strerror(errno));
  }

  launched_program_count = 0;
}
