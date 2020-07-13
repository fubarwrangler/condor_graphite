/**
 * Functions to send the metrics over the network (using a buffer for TCP) and
 * to turn the cgroup structure into actual metric strings
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>

#include "metrics.h"
#include "util.h"
#include "cgroup.h"


#define BUFSIZE 4096


int debug = 0;
char buf[BUFSIZE];
static size_t buf_used = 0;

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

/**
 * Sanitize hostname for metric inclusion (. -> _)
 * WARNING: Returns new storage
 */
static char *sanitize_host(const char *host)
{
	char *q = xstrdup(host);
	char *p = q;
	do {
		if(*p == '.')
			*p = '_';
	} while(*++p);
	return q;
}


void send_group_metrics(struct condor_group *g, const char *hostname,
			const char *ns, int fd,
			int (*send_fn)(int, const char *, uint64_t))
{
	char *base;
	char *metric;
	size_t b_len;
	char *sanitized_host = sanitize_host(hostname);

	b_len = strlen(ns) +
			strlen(sanitized_host) +
			strlen(g->slot_name) +
			4;
	base = xcalloc(b_len);
	metric = xcalloc(b_len + 32);

	snprintf(base, b_len, "%s.%s.%s",
		 ns, sanitized_host, g->slot_name);
	free(sanitized_host);

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

	snprintf(metric, b_len + 32, "%s.cache", base);
	(*send_fn)(fd, metric, g->cache_used);

	snprintf(metric, b_len + 32, "%s.swap", base);
	(*send_fn)(fd, metric, g->swap_used);

	snprintf(metric, b_len + 32, "%s.softmemlimit", base);
	(*send_fn)(fd, metric, g->mem_soft_limit);

	free(base);
	free(metric);
}
