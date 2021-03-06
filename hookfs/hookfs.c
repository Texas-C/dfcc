#define _GNU_SOURCE /* needed to get RTLD_NEXT defined in dlfcn.h */
#include <alloca.h>  // alloca
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include <dirent.h>
#include <dlfcn.h>  // RTLD_NEXT
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <linux/limits.h>  // PATH_MAX

#include "common/macro.h"
#include "serializer.h"
#include "socket.h"

/**
 * @defgroup Hookfs Hookfs
 * @{
 */


struct Serializer serdes;
struct Socket sock;

#define check(func) \
should (errno == 0) otherwise { \
  perror(# func); \
  exit(-1); \
}

#if !DEBUG
#define DBG(...)
#else
#define DBG(...) fprintf(stderr, __VA_ARGS__)
#endif


/**
 * @brief Resolves the real function provided by libc.
 *
 * @param symbol string of function name
 * @return libc function `symbol`
 */
static void *resolve (const char *symbol) {
  void *real = dlsym(RTLD_NEXT, symbol);
  should (real != NULL) otherwise {
    fprintf(stderr, "Error when resloving symbol %s: %s\n", symbol, dlerror());
    exit(255);
  }
  return real;
}


/**
 * @brief Wraps a libc function.
 *
 * @code{.c}
  WRAP(int, open) (const char *path, int oflag, mode_t mode) {
    ...
    int real_ret = libc_open(path, oflag, mode);
    ...
  }
 * @endcode
 *
 * @param type the return type of function `symbol`
 * @param symbol a libc function
 */
#define WRAP(type, symbol) \
  static void *resolve_ ## symbol (void) { \
    return resolve(# symbol); \
  } \
  type libc_ ## symbol () __attribute__ ((ifunc ("resolve_" # symbol))); \
  type symbol

#define HOOK(type, func, args, ...) { \
  mtx_lock(&sock.mtx); \
  serialize_literal(&serdes, # func); \
  __VA_ARGS__ \
  serialize_end(&serdes); \
  mtx_unlock(&sock.mtx); \
  \
  type ret = libc_ ## func args; \
  return ret; \
}

#define HOOK_PATH(type, func, args, path, ...) { \
  mtx_lock(&sock.mtx); \
  serialize_literal(&serdes, # func); \
  __VA_ARGS__ \
  serialize_end(&serdes); \
  \
  char recv_buf[Hookfs_MAX_TOKEN_LEN]; \
  ssize_t new_path_len = deserialize_length(&serdes, deserialize_next(&serdes, NULL)); \
  deserialize(&serdes, recv_buf, (size_t) new_path_len, NULL); \
  mtx_unlock(&sock.mtx); \
  \
  if (new_path_len > 0) { \
    (path) = recv_buf; \
  } \
  type ret = libc_ ## func args; \
  return ret; \
}


WRAP(int, execl) (const char *path, const char *arg0, ...) {
  va_list ap;

  int argc = 2;
  va_start(ap, arg0);
  while (va_arg(ap, char*))
    argc++;
  va_end(ap);

  const char *argv[argc];
  argv[0] = arg0;
  va_start(ap, arg0);
  for (int i = 1; i < argc; i++)
    argv[i] = va_arg(ap, char*);
  va_end(ap);
  return execve(path, (char *const*)argv, environ);
}


WRAP(int, execle) (const char *path, const char *arg0, .../*, char *const envp[]*/) {
  va_list ap;

  int argc = 2;
  va_start(ap, arg0);
  while (va_arg(ap, char*))
    argc++;
  va_end(ap);

  const char *argv[argc];
  argv[0] = arg0;
  va_start(ap, arg0);
  for (int i = 1; i < argc; i++)
    argv[i] = va_arg(ap, char*);
  char *const* envp = va_arg(ap, char *const*);
  va_end(ap);
  return execve(path, (char *const*)argv, envp);
}


WRAP(int, execlp) (const char *file, const char *arg0, ...) {
  va_list ap;

  int argc = 2;
  va_start(ap, arg0);
  while (va_arg(ap, char*))
    argc++;
  va_end(ap);

  const char *argv[argc];
  argv[0] = arg0;
  va_start(ap, arg0);
  for (int i = 1; i < argc; i++)
    argv[i] = va_arg(ap, char*);
  va_end(ap);
  return execvp(file, (char *const*)argv);
}


WRAP(int, execv) (const char *path, char *const argv[]) {
  return execve(path, argv, environ);
}


WRAP(int, execve) (const char *path, char *const argv[], char *const envp[])
HOOK(int, execve, (path, argv, envp),
  serialize_string(&serdes, path);
  serialize_strv(&serdes, argv);
  serialize_strv(&serdes, envp);
)


WRAP(int, execvp) (const char *file, char *const argv[])
HOOK(int, execvp, (file, argv),
  serialize_string(&serdes, file);
  serialize_strv(&serdes, argv);
)


WRAP(int, access) (const char *pathname, int mode)
HOOK_PATH(int, access, (pathname, mode), pathname,
  serialize_string(&serdes, pathname);
  serialize_printf(&serdes, "%d", mode);
)


WRAP(int, stat) (const char *pathname, struct stat *statbuf)
HOOK_PATH(int, stat, (pathname, statbuf), pathname,
  serialize_string(&serdes, pathname);
  serialize_numerical(&serdes, (uint64_t) statbuf);
)


WRAP(int, lstat) (const char *pathname, struct stat *statbuf)
HOOK_PATH(int, lstat, (pathname, statbuf), pathname,
  serialize_string(&serdes, pathname);
  serialize_numerical(&serdes, (uint64_t) statbuf);
)


/*
WRAP(int, open) (const char *path, int oflag, mode_t mode)
HOOK_PATH(int, open, (path, oflag, mode), path,
  serialize_string(&serdes, path);
  serialize_printf(&serdes, "%d", oflag);
  serialize_printf(&serdes, "%d", mode);
)


WRAP(int, open64) (const char *path, int oflag, mode_t mode)
HOOK_PATH(int, open64, (path, oflag, mode), path,
  serialize_string(&serdes, path);
  serialize_printf(&serdes, "%d", oflag);
  serialize_printf(&serdes, "%d", mode);
)
*/


WRAP(FILE *, fopen) (const char *filename, const char *mode)
HOOK_PATH(FILE *, fopen, (filename, mode), filename,
  serialize_string(&serdes, filename);
  serialize_string(&serdes, mode);
)


WRAP(FILE *, fopen64) (const char *filename, const char *mode)
HOOK_PATH(FILE *, fopen64, (filename, mode), filename,
  serialize_string(&serdes, filename);
  serialize_string(&serdes, mode);
)


/*
WRAP(int, fclose) (FILE *stream)
HOOK(int, fclose, "0", "%p\n", stream)


WRAP(int, fclose64) (FILE *stream)
HOOK(int, fclose64, "0", "%p\n", stream)
*/


WRAP(FILE *, freopen) (const char *filename, const char *mode, FILE *stream)
HOOK_PATH(FILE *, freopen, (filename, mode, stream), filename,
  serialize_string(&serdes, filename);
  serialize_string(&serdes, mode);
  serialize_numerical(&serdes, (uint64_t) stream);
)


WRAP(FILE *, freopen64) (const char *filename, const char *mode, FILE *stream)
HOOK_PATH(FILE *, freopen64, (filename, mode, stream), filename,
  serialize_string(&serdes, filename);
  serialize_string(&serdes, mode);
  serialize_numerical(&serdes, (uint64_t) stream);
)


WRAP(int, rename) (const char *oldname, const char *newname)
HOOK(int, rename, (oldname, newname),
  serialize_string(&serdes, oldname);
  serialize_string(&serdes, newname);
)


WRAP(int, unlink) (const char *filename)
HOOK(int, unlink, (filename),
  serialize_string(&serdes, filename);
)


WRAP(int, remove) (const char *filename)
HOOK(int, remove, (filename),
  serialize_string(&serdes, filename);
)


WRAP(FILE *, tmpfile) (void)
HOOK(FILE *, tmpfile, ())


WRAP(FILE *, tmpfile64) (void)
HOOK(FILE *, tmpfile64, ())


WRAP(DIR *, opendir) (const char *name)
HOOK_PATH(DIR *, opendir, (name), name,
  serialize_string(&serdes, name);
)


/*
WRAP(ssize_t, read) (int fd, void *buf, size_t count) {
  char pathbuf[512];
  snprintf(pathbuf, sizeof pathbuf, "/proc/self/fd/%d", fd);
  char pathname[512];
  readlink(pathbuf, pathname, sizeof pathname);
  fprintf(stderr, "reading: %d => %s count %lu\n", fd, pathname, count);
  return libc_read(fd, buf, count);
}
*/


static void __attribute__ ((destructor)) hookfs_del () {
  Socket_send(&sock, NULL, 0);
  Socket_destroy(&sock);
}


static void __attribute__ ((constructor)) hookfs_init () {
  char *sock_path = getenv("HOOKFS_SOCK_PATH");
  should (sock_path != NULL) otherwise {
    fputs("No HOOKFS_SOCK_PATH specified!\n", stderr);
    exit(255);
  }

  char *hookfs_ns = getenv("HOOKFS_NS");
  should (hookfs_ns != NULL) otherwise {
    fputs("No HOOKFS_NS specified!\n", stderr);
    exit(255);
  }

  should (Socket_init(&sock, sock_path) == 0) otherwise {
    exit(255);
  }

  serdes.istream = &sock;
  serdes.ostream = &sock;
  serdes.write = (Serializer__write_t) Socket_send;
  serdes.read = (Serializer__read_t) Socket_recv;

  serialize_literal(&serdes, "-id");
  serialize_string(&serdes, hookfs_ns);
  uint32_t pid = getpid();
  serialize_numerical(&serdes, pid);
  serialize_end(&serdes);
}


/**@}*/
