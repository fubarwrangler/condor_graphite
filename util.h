#ifndef _UTIL_H
#define _UTIL_H

#include "cgroup.h"
#include <stdbool.h>

/* Utility methods to send to metrics servers over TCP/UDP with configurable
 * buffering
 */
int server_connect(const char *server, const char *port, int ai_socktype);

/* Safe malloc/calloc */
void *xcalloc(size_t len);
char *xstrdup(const char *s);

/* Log message and exit program */
void log_exit(const char *fmt, ...);

/* Join strings on a path separator, returning a pointer to static storage */
const char *join_path(const char *c1, const char *c2);

extern int debug;

#endif
