#ifndef _UTIL_H
#define _UTIL_H

#include "cgroup.h"
#include <stdbool.h>

#define STREQ(a, b)	(0 == strcmp(a, b))
#define STRNEQ(a, b)	(!STREQ(a,b))

/* Utility methods to send to metrics servers over TCP/UDP with configurable
 * buffering
 */
int server_connect(const char *server, const char *port, int ai_socktype);

/* Safe malloc/calloc */
void *xcalloc(size_t len);
char *xstrdup(const char *s);

/* Log message and exit program */
void log_exit(const char *fmt, ...)  __attribute__((noreturn));

/* Join strings on a path separator, returning a pointer to static storage */
const char *join_path(const char *c1, const char *c2);

extern int debug;

#endif
