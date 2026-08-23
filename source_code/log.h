#ifndef SWORDFISH_LOG_H
#define SWORDFISH_LOG_H

//INFO on tty3 there is no terminal to read: the compositor owns the display and
//everything printf writes to the console is either invisible or scrolled away by
//the next frame. so every message goes to a file - /tmp/swordfish.log, or
//$SWORDFISH_LOG - that survives the run and can be read from another VT

typedef enum LogLevel {
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR,
} LogLevel;

//opens the log file, keeps the previous run as <path>.old, and arms the crash
//handler. call it first thing in main(), before anything that can fail
void log_init(void);

//sends stdout and stderr into the log file as well, so the printf() calls all
//over swordfish and the vulkan validation layer's own output are captured
//too. only for the bare DRM path - on the pway path the terminal is real
void log_redirect_stdio(void);

void log_end(void);

void log_write(LogLevel level, const char *file, int line, const char *format,
               ...) __attribute__((format(printf, 4, 5)));

#define log_debug(...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define log_info(...) log_write(LOG_LEVEL_INFO, __FILE__, __LINE__, __VA_ARGS__)
#define log_warn(...) log_write(LOG_LEVEL_WARN, __FILE__, __LINE__, __VA_ARGS__)
#define log_error(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __VA_ARGS__)

#endif // !SWORDFISH_LOG_H
