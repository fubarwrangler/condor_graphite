#ifndef _UTIL_H
#define _UTIL_H

#include <stdbool.h>

/* Utility methods to send to metrics servers over TCP/UDP with configurable
 * buffering
 */
int server_connect(const char *server, const char *port, int ai_socktype);
int util_metric_send(int fd, const char *metric, bool buffer);
void buf_close(int fd);

/* Safe malloc/calloc */
void *xcalloc(size_t len);

#endif
