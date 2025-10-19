#include "build.h"

#include <errno.h>
#include <fcntl.h>
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

void *call_make(void *none) {
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
    execlp("make", "make", NULL);
    perror("execlp");
    exit(EXIT_FAILURE);
  } else { // parent
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

  finished_build = true;

}
