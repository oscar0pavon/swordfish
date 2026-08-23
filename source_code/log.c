#include "log.h"

#include <errno.h>
#include <execinfo.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOG_PATH_DEFAULT "/tmp/swordfish.log"
#define LOG_LINE_MAX 1024
#define LOG_BACKTRACE_MAX 32

static int log_file = -1;

//where the records are echoed as well as written. the terminal on the pway
//path, and -1 once log_redirect_stdio() has pointed the terminal's own file
//descriptors at the log file, which would otherwise print everything twice
static int mirror_file = STDERR_FILENO;

static LogLevel minimum_level = LOG_LEVEL_DEBUG;
static bool sync_every_record = false;
static struct timespec start_time;

static const char *level_names[] = {"DEBUG", "INFO ", "WARN ", "ERROR"};

static void write_all(int file, const char *data, size_t size) {
  while (size > 0) {
    ssize_t written = write(file, data, size);
    if (written <= 0) {
      if (written < 0 && errno == EINTR)
        continue;
      return;
    }
    data += written;
    size -= written;
  }
}

static void write_record(const char *data, size_t size) {
  if (log_file >= 0) {
    write_all(log_file, data, size);
    //INFO for chasing a lockup that takes the machine with it: the page cache
    //keeps a record written by a process that segfaults, but not one that dies
    //with the kernel
    if (sync_every_record)
      fdatasync(log_file);
  }
  if (mirror_file >= 0)
    write_all(mirror_file, data, size);
}

static double seconds_since_start(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (now.tv_sec - start_time.tv_sec) +
         (now.tv_nsec - start_time.tv_nsec) / 1000000000.0;
}

//"./wayland_window/window.c" is how the makefile spells it on the command line,
//and __FILE__ repeats it. the file name on its own is what is worth reading
static const char *file_name_only(const char *path) {
  const char *slash = strrchr(path, '/');
  return slash ? slash + 1 : path;
}

static LogLevel level_from_name(const char *name) {
  if (strcmp(name, "debug") == 0)
    return LOG_LEVEL_DEBUG;
  if (strcmp(name, "info") == 0)
    return LOG_LEVEL_INFO;
  if (strcmp(name, "warn") == 0)
    return LOG_LEVEL_WARN;
  if (strcmp(name, "error") == 0)
    return LOG_LEVEL_ERROR;
  return LOG_LEVEL_DEBUG;
}

void log_write(LogLevel level, const char *file, int line, const char *format,
               ...) {
  if (level < minimum_level)
    return;

  //compositor.c logs a poll() failure with %m, and glibc reads errno where the
  //format is expanded - which is after the timestamp below has had its turn
  int caller_errno = errno;

  char record[LOG_LINE_MAX];

  int length = snprintf(record, sizeof(record), "[%9.3f] %s %s:%d: ",
                        seconds_since_start(), level_names[level],
                        file_name_only(file), line);
  if (length < 0)
    return;
  if (length > (int)sizeof(record) - 2)
    length = sizeof(record) - 2;

  va_list arguments;
  va_start(arguments, format);
  errno = caller_errno;
  int message_length =
      vsnprintf(record + length, sizeof(record) - length - 1, format, arguments);
  va_end(arguments);

  if (message_length > 0) {
    length += message_length;
    if (length > (int)sizeof(record) - 2)
      length = sizeof(record) - 2;
  }

  if (record[length - 1] != '\n')
    record[length++] = '\n';

  write_record(record, length);
}

//INFO everything below runs from a signal handler on a process that is already
//dying, so it is write() and nothing else - no printf, no malloc. backtrace()
//allocates the first time it is called, which is why log_init() calls it once
//while the program is still healthy
static void write_number(int file, unsigned long value) {
  char digits[24];
  int index = sizeof(digits);

  digits[--index] = '\n';
  if (value == 0)
    digits[--index] = '0';
  while (value > 0 && index > 0) {
    digits[--index] = '0' + (value % 10);
    value /= 10;
  }

  write_all(file, digits + index, sizeof(digits) - index);
}

static void handle_crash_signal(int signal_number) {
  static const char message[] = "\n=== swordfish died on signal ";

  write_record(message, sizeof(message) - 1);
  if (log_file >= 0)
    write_number(log_file, signal_number);
  if (mirror_file >= 0)
    write_number(mirror_file, signal_number);

  void *frames[LOG_BACKTRACE_MAX];
  int frame_count = backtrace(frames, LOG_BACKTRACE_MAX);
  if (log_file >= 0)
    backtrace_symbols_fd(frames, frame_count, log_file);
  if (mirror_file >= 0)
    backtrace_symbols_fd(frames, frame_count, mirror_file);

  //let the default handler produce the core dump and the real exit status
  signal(signal_number, SIG_DFL);
  raise(signal_number);
}

static void arm_crash_handler(void) {
  void *frames[LOG_BACKTRACE_MAX];
  backtrace(frames, LOG_BACKTRACE_MAX);

  signal(SIGSEGV, handle_crash_signal);
  signal(SIGABRT, handle_crash_signal);
  signal(SIGBUS, handle_crash_signal);
  signal(SIGFPE, handle_crash_signal);
  signal(SIGILL, handle_crash_signal);
}

void log_init(void) {
  clock_gettime(CLOCK_MONOTONIC, &start_time);

  const char *level = getenv("SWORDFISH_LOG_LEVEL");
  if (level)
    minimum_level = level_from_name(level);

  sync_every_record = getenv("SWORDFISH_LOG_SYNC") != NULL;

  const char *path = getenv("SWORDFISH_LOG");
  if (!path)
    path = LOG_PATH_DEFAULT;

  //the run that crashed is usually the one worth reading, and it is easy to
  //start the next one before reading it
  char previous_path[PATH_MAX];
  snprintf(previous_path, sizeof(previous_path), "%s.old", path);
  rename(path, previous_path);

  log_file = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (log_file < 0) {
    perror("Failed to open the log file");
    return;
  }

  arm_crash_handler();

  time_t wall_clock = time(NULL);
  char stamp[64];
  strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S",
           localtime(&wall_clock));

  log_info("Swordfish started %s, pid %d, log %s", stamp, getpid(), path);
}

void log_redirect_stdio(void) {
  if (log_file < 0)
    return;

  log_info("stdout and stderr go to the log file from here on");

  //everything printf writes lands in the log file from now on, so the mirror
  //would write every record to the same file a second time
  mirror_file = -1;

  fflush(stdout);
  fflush(stderr);

  dup2(log_file, STDOUT_FILENO);
  dup2(log_file, STDERR_FILENO);

  //a log file is a regular file, so stdout would be fully buffered and the last
  //4096 bytes before a crash - the interesting ones - would never be written
  setvbuf(stdout, NULL, _IOLBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
}

void log_end(void) {
  if (log_file < 0)
    return;

  log_info("Swordfish closed after %.3f seconds", seconds_since_start());

  fflush(stdout);
  fflush(stderr);

  close(log_file);
  log_file = -1;
  mirror_file = -1;
}
