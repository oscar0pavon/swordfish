#include "launch.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

//this used to sit in build.c beside call_make(), which ran `make` and piped
//its output into the 3D scene. the scene is 3dtop's now and the build display
//went with it; spawning a program from a keybinding is the compositor's own
//job and is all that is left
//
//a blocking spawn is exactly what a keybinding must not do: it runs on the
//input thread, so waiting there stops pway from being pumped and freezes the
//window until the program is closed. double fork so the grandchild is
//reparented to init and nothing has to be reaped later
void launch_program(const char* command){

  pid_t pid = fork();
  if (pid == -1) {
    perror("fork");
    return;
  }

  if (pid == 0) {
    if (fork() == 0) {
      //own session, so the child does not take our controlling terminal
      setsid();
      execlp(command, command, NULL);
      perror("execlp");
      _exit(EXIT_FAILURE);
    }
    _exit(EXIT_SUCCESS);
  }

  printf("Launching %s\n", command);

  //only the short lived middle child is waited for
  waitpid(pid, NULL, 0);
}
