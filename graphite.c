#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <syslog.h>
#include <unistd.h>
#include <assert.h>

#include "graphite.h"

static time_t _current_time = 0;

static int (_contype) = 0;

void graphite_init(enum graphite_contype ctype)
{
	_current_time = time(NULL);
	_contype = ctype;
	openlog("graphite-lib", LOG_ODELAY | LOG_PID, LOG_DAEMON);
}

int graphite_connect(const char *server, const char *port)
{
	if(_contype == GRAPHITE_TCP) {
		return server_connect(server, port, SOCK_STREAM);
	} else {
		return server_connect(server, port, SOCK_DGRAM);
	}
}

void graphite_close(int fd)
{
	if(_contype == GRAPHITE_TCP)	{
		buf_close(fd);
		if (shutdown(fd, SHUT_RDWR) != 0)
			perror("TCP Shutdown");
	}
	if(close(fd) < 0)
		perror("Close fd");
}

static char *_make_metric(const char *name, const char *val)
{
	/* lengths + (generous)len of time + spaces + null */
	size_t len = strlen(name) + strlen(val) + 16 + 2 + 1;
	char *str;

	str = xcalloc(len);

	assert(_current_time > 0);

	/* <metric.name.path> <value> <timestamp> */
	snprintf(str, len, "%s %s %ld\n", name, val, _current_time);

	return str;
}

static int _send_metric(int fd, const char *m, const char *v)
{
	char *mstr = _make_metric(m, v);
	int rv;

	rv = util_metric_send(fd, mstr, (_contype == GRAPHITE_TCP));
	free(mstr);

	return rv;
}

#define VAL_BUF 24 /* 64-bit values go up to 10^19, so this should be enough */

int graphite_send_uint(int fd, const char *metric, uint64_t value)
{
	char s[VAL_BUF];
	if(value & 0xff00000000000000)	{
		syslog(LOG_ERR, "Really large int for graphite: %s = %lx (%lu)",
		       metric, value, value);
	}
	snprintf(s, sizeof(s), "%lu", value);
	return _send_metric(fd, metric, s);
}

int graphite_send_int(int fd, const char *metric, int64_t value)
{
	char s[VAL_BUF];
	snprintf(s, sizeof(s), "%ld", value);
	return _send_metric(fd, metric, s);
}

int graphite_send_float(int fd, const char *metric, float value)
{
	char s[VAL_BUF];
	snprintf(s, sizeof(s), "%f", value);
	return _send_metric(fd, metric, s);
}
