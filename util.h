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

extern int debug;

#endif
