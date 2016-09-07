#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>

#include "graphite.h"

static time_t _current_time = 0;
int graphite_debug = 0;

void graphite_init(void)
{
	_current_time = time(NULL);
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
	hints.ai_socktype = SOCK_DGRAM;

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
		fprintf(stderr, "Error creating UDP socket to %s\n", server);
		exit(EXIT_FAILURE);
	}

	return sfd;
}

static int _send_metric(int fd, const char *name, const char *val_str)
{
	/* lengths + (generous)len of time + spaces + null */
	char str[strlen(name) + strlen(val_str) + 16 + 2 + 1];
	ssize_t len;

	assert(_current_time > 0);

	/* <metric.name.path> <value> <timestamp> */
	snprintf(str, sizeof(str), "%s %s %ld", name, val_str, _current_time);
	len = strlen(str);

	if(graphite_debug) {
		puts(str);
		return 0;
	}
	if (send(fd, str, len, 0) != len) {
		fprintf(stderr, "short / failed UDP send for %s\n", name);
		return -1;
	}
	return 0;
}

#define VAL_BUF 24 /* 64-bit values go up to 10^19, so this should be enough */

int graphite_send_uint(int fd, const char *metric, uint64_t value)
{
	char s[VAL_BUF];
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
