#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

#include "util.h"
#include "cgroup.h"

#define BUFSIZE 1408

int debug = 0;
char buf[BUFSIZE];
static size_t buf_used = 0;

void send_group_metrics(struct condor_group *g, const char *hostname,
			const char *ns, int fd,
			int (*send_fn)(int, const char *, uint64_t))
{
	char *base;
	char *metric;
	char sanitized_host[sizeof(hostname)];
	char *p = sanitized_host;
	size_t b_len;

	strcpy(sanitized_host, hostname);
	do {
		if(*p == '.') *p = '_';
	} while(*++p);

	b_len = strlen(ns) +
			strlen(sanitized_host) +
			strlen(g->slot_name) +
			4;
	base = xcalloc(b_len);
	metric = xcalloc(b_len + 32);

	snprintf(base, b_len, "%s.%s.%s",
		 ns, sanitized_host, g->slot_name);

	snprintf(metric, b_len + 32, "%s.starttime", base);
	(*send_fn)(fd, metric, g->start_time);

	snprintf(metric, b_len + 32, "%s.cpu_shares", base);
	(*send_fn)(fd, metric, g->cpu_shares);

	snprintf(metric, b_len + 32, "%s.tasks", base);
	(*send_fn)(fd, metric, g->num_tasks);

	snprintf(metric, b_len + 32, "%s.procs", base);
	(*send_fn)(fd, metric, g->num_procs);

	snprintf(metric, b_len + 32, "%s.cpu_user", base);
	(*send_fn)(fd, metric, g->user_cpu_usage);

	snprintf(metric, b_len + 32, "%s.cpu_sys", base);
	(*send_fn)(fd, metric, g->sys_cpu_usage);

	snprintf(metric, b_len + 32, "%s.rss", base);
	(*send_fn)(fd, metric, g->rss_used);

	snprintf(metric, b_len + 32, "%s.swap", base);
	(*send_fn)(fd, metric, g->swap_used);

	free(base);
	free(metric);
}

int server_connect(const char *server, const char *port, int ai_socktype)
{
	struct addrinfo hints = {0};
	struct addrinfo *result, *rp;
	int sfd, s;

	if (ai_socktype != SOCK_STREAM || ai_socktype != SOCK_DGRAM)	{
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

/* Sends buffer over TCP connections */
static void _flush_buf(int fd)
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
int util_metric_send(int fd, const char *metric, bool buffer)
{
	ssize_t len = strlen(metric);

	if(debug) {
		printf("%s", metric);
		return 0;
	}

	if(buffer)	{
		assert(len < BUFSIZE - 1);
		if(buf_used + len >= BUFSIZE)	{
			_flush_buf(fd);
		}
		strncpy(buf + buf_used, metric, len);
		buf_used += len;
	} else {
		if (send(fd, metric, len, 0) != len)	{
			fprintf(stderr, "short / failed send for %s\nerror: "
					"%s\n", metric, strerror(errno));
			return -1;
		}
	}
	return 0;
}

void buf_close(int fd)
{
	if(buf_used > 0)	{
		_flush_buf(fd);
	}
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
