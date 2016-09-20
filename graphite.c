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
int graphite_debug = 0;

static char *buf;
static size_t buf_used = 0;
static int _contype = 0;

void graphite_init(enum graphite_contype ctype)
{
	_current_time = time(NULL);
	_contype = ctype;
	openlog("graphite-lib", LOG_ODELAY | LOG_PID, LOG_DAEMON);
	if(_contype == GRAPHITE_TCP)	{
		if((buf = calloc(1, GRAPHITE_BUFSIZE)) == NULL)	{
			fprintf(stderr, "graphite_init: malloc failure\n");
			exit(EXIT_FAILURE);
		}
	}
}

int graphite_connect(const char *server, const char *port)
{
	struct addrinfo hints = {0};
	struct addrinfo *result, *rp;
	int sfd, s;

	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	hints.ai_family = AF_UNSPEC;        /* Allows IPv4 or IPv6 */
	hints.ai_socktype = (_contype == GRAPHITE_TCP) ?
			SOCK_STREAM : SOCK_DGRAM;

	s = getaddrinfo(server, port, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "Error looking up %s:%s...exit\n",
			server, port);
		exit(EXIT_FAILURE);
	}

	/* Walk through returned list until we find an address structure
	 * that can be used to successfully connect a socket */
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1)
			continue;

		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
			break;

		/* Connect failed: close this socket and try next address */
		close(sfd);
	}

	freeaddrinfo(result);

	if (rp == NULL)	{
		fprintf(stderr, "Error creating %s socket to %s\n",
			(_contype == GRAPHITE_UDP) ? "UDP" : "TCP", server);
		exit(EXIT_FAILURE);
	}

	return sfd;
}
/* Sends buffer over TCP connections */
static void send_buf(int fd)
{
	size_t sent = 0;
	ssize_t this_send;

	while(sent < buf_used)	{
		this_send = send(fd, buf + sent, buf_used - sent, 0);
		if(this_send < 0 && errno != EINTR)	{
			fprintf(stderr, "send() error: %s", strerror(errno));
			exit(EXIT_FAILURE);
		}
		sent += this_send;
	}
	buf_used = 0;
}

/**
 * Construct the actual metric-string and either send over UDP or add to
 * buffer to be send in bulk over TCP
 */
static int _send_metric(int fd, const char *name, const char *val_str)
{
	/* lengths + (generous)len of time + spaces + null */
	char str[strlen(name) + strlen(val_str) + 16 + 2 + 1];
	ssize_t len;

	assert(_current_time > 0);

	/* <metric.name.path> <value> <timestamp> */
	snprintf(str, sizeof(str), "%s %s %ld\n", name, val_str, _current_time);
	len = strlen(str);


	if(graphite_debug) {
		printf("%s", str);
		return 0;
	}

	if(_contype == GRAPHITE_TCP)	{
		assert(len < (ssize_t)GRAPHITE_BUFSIZE - 1);
		if(buf_used + len >= GRAPHITE_BUFSIZE)	{
			send_buf(fd);
		}
		strncpy(buf + buf_used, str, len);
		buf_used += len;
	} else {
		if (send(fd, str, len, 0) != len)	{
			fprintf(stderr, "short / failed UDP send for %s\n"
					"error: %s\n", name, strerror(errno));
			return -1;
		}
	}
	return 0;
}


void graphite_close(int fd)
{
	if(_contype == GRAPHITE_TCP)	{
		if(buf_used > 0)
			send_buf(fd);
		if (shutdown(fd, SHUT_RDWR) != 0)
			perror("TCP Shutdown");
		buf_used = 0;
		free(buf);
	}
	if(close(fd) < 0)
		perror("Close fd");
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
