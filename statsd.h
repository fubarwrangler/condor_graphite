#ifndef _STATSD_H
#define _STATSD_H
#include "util.h"

#define STATSD_BUFSIZE 1430

int	statsd_connect(const char *server, const char *port);
int	statsd_send_int(int fd, const char *metric, int value);
int	statsd_send_uint(int fd, const char *metric, uint64_t value);
void	statsd_close(int fd);

#endif
