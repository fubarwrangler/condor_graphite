/**
 * Functions to format a metric as statsd can understand
 */
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <unistd.h>

#include "graphite.h"

int statsd_connect(const char *server, const char *port)
{
	return server_connect(server, port, SOCK_DGRAM);
}

void statsd_close(int fd)
{
	buf_close(fd);
	if(close(fd) < 0)
		perror("Close fd");
}

static char *_make_metric(const char *name, const char *val)
{
	size_t len = strlen(name) + strlen(val) + 8;
	char *str;

	str = xcalloc(len);

	/* <metric.name.path>:<value>|<type> */
	snprintf(str, len, "%s:%s|c\n", name, val);

	return str;
}

static int _send_metric(int fd, const char *m, const char *v)
{
	char *mstr = _make_metric(m, v);
	int rv;

	rv = util_metric_send(fd, mstr, true);
	free(mstr);

	return rv;
}

#define VAL_BUF 24 /* 64-bit values go up to 10^19, so this should be enough */

int statsd_send_uint(int fd, const char *metric, uint64_t value)
{
	char s[VAL_BUF];
	snprintf(s, sizeof(s), "%ld", value);
	return _send_metric(fd, metric, s);
}

int statsd_send_int(int fd, const char *metric, int value)
{
	char s[VAL_BUF];
	snprintf(s, sizeof(s), "%d", value);
	return _send_metric(fd, metric, s);
}
