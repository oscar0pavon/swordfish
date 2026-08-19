#include "build.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include "swordfish.h"

#define READ_END 0
#define WRITE_END 1
#define BUFFER_SIZE 4096

void handle_child_output(int pipe_fd) {
  char buffer[BUFFER_SIZE];
  ssize_t bytes_read;
  bytes_read = read(pipe_fd, buffer, BUFFER_SIZE);
  if (bytes_read > 0) {
    write(STDOUT_FILENO, buffer, bytes_read);
  }
}


//call_program() blocks until the child exits, which is what call_make() wants
//and exactly what a keybinding must not do: it runs on the input thread, so
//waiting there stops pway from being pumped and freezes the window until the
//terminal is closed. double fork so the grandchild is reparented to init and
//nothing has to be reaped later
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


void call_program(const char* command){

  int pipefd[2];
  pid_t pid;

  if (pipe(pipefd) == -1) {
    perror("pipe");
    exit(EXIT_FAILURE);
  }

  pid = fork();
  if (pid == -1) {
    perror("fork");
    exit(EXIT_FAILURE);
  }

  if (pid == 0) { // child
    close(pipefd[READ_END]);
    dup2(pipefd[WRITE_END], STDOUT_FILENO);
    dup2(pipefd[WRITE_END], STDERR_FILENO);
    close(pipefd[WRITE_END]);
    execlp(command, command, NULL);
    perror("execlp");
    //pthread_exit("Error executing command");
  } else { // parent
    printf("Running %s\n",command);
    close(pipefd[WRITE_END]);

    fd_set fds;
    int max_fd = pipefd[READ_END];
    int status;

    while (1) {
      FD_ZERO(&fds);
      FD_SET(pipefd[READ_END], &fds);

      struct timeval timeout = {1, 0}; // 1-second timeout

      int ready = select(max_fd + 1, &fds, NULL, NULL, &timeout);

      if (ready == -1) {
        if (errno == EINTR) {
          continue;
        }
        perror("select");
        break;
      }

      if (ready > 0) {
        if (FD_ISSET(pipefd[READ_END], &fds)) {
          handle_child_output(pipefd[READ_END]);
        }
      } else {
        printf("Parent process is doing other work...\n");
      }

      pid_t child_status = waitpid(pid, &status, WNOHANG);
      if (child_status == pid) {
        handle_child_output(pipefd[READ_END]);
        break;
      }
    }

    waitpid(pid, &status, 0);
    close(pipefd[READ_END]);
  }

}


void *call_make(void *none) {
  call_program("make");
  finished_build = true;

}
