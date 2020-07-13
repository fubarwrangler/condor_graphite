#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <assert.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include "util.h"
#include "cgroup.h"



int server_connect(const char *server, const char *port, int ai_socktype)
{
	struct addrinfo hints = {0};
	struct addrinfo *result, *rp;
	int sfd, s;

	if (ai_socktype != SOCK_STREAM && ai_socktype != SOCK_DGRAM)	{
		fputs("BUG!: socket-type must be STREAM/DGRAM!\n", stderr);
		exit(EXIT_FAILURE);
	}

	hints.ai_canonname = NULL;
	hints.ai_addr = NULL;
	hints.ai_next = NULL;
	hints.ai_family = AF_UNSPEC;        /* Allows IPv4 or IPv6 */
	hints.ai_socktype = ai_socktype;

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
			(ai_socktype == SOCK_DGRAM) ? "UDP" : "TCP", server);
		exit(EXIT_FAILURE);
	}

	return sfd;
}


const char* join_path(const char *c1, const char *c2)
{
	static char buf[512];
	snprintf(buf, sizeof(buf), "%s/%s", c1, c2);
	return buf;
}

void log_exit(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(EXIT_FAILURE);
}


void *xcalloc(size_t len)
{
	void *p;

	if((p = calloc(1, len)) == NULL)	{
		fprintf(stderr, "util: malloc failure\n");
		exit(EXIT_FAILURE);
	}

	return p;
}

char *xstrdup(const char *s)
{
	char *n = strdup(s);
	if(n == NULL)	{
		fprintf(stderr, "util: strdup malloc failure\n");
		exit(EXIT_FAILURE);
	}
	return n;
}
