#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <inttypes.h>

#include "statsd.h"
#include "cgroup.h"
#include "util.h"

static char hostname[256];
static char *root_ns = "htcondor.cgroups";
static char *cgroup_name = "condor";
static int debug = 0;

static inline int min(int a, int b) { return (a < b) ? a : b; }

static void send_group_metrics(struct condor_group *g, int fd)
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

	b_len = strlen(root_ns) +
			strlen(sanitized_host) +
			strlen(g->slot_name) +
			4;
	base = xcalloc( b_len );
	metric = xcalloc(b_len + 32);

	snprintf(base, b_len, "%s.%s.%s",
		 root_ns, sanitized_host, g->slot_name);

	snprintf(metric, b_len + 32, "%s.starttime", base);
	statsd_send_int(fd, metric, g->start_time);

	snprintf(metric, b_len + 32, "%s.cpu_shares", base);
	statsd_send_int(fd, metric, g->cpu_shares);

	snprintf(metric, b_len + 32, "%s.tasks", base);
	statsd_send_int(fd, metric, g->num_tasks);

	snprintf(metric, b_len + 32, "%s.procs", base);
	statsd_send_int(fd, metric, g->num_procs);

	snprintf(metric, b_len + 32, "%s.cpu_user", base);
	statsd_send_int(fd, metric, g->user_cpu_usage);

	snprintf(metric, b_len + 32, "%s.cpu_sys", base);
	statsd_send_int(fd, metric, g->sys_cpu_usage);

	snprintf(metric, b_len + 32, "%s.rss", base);
	statsd_send_int(fd, metric, g->rss_used);

	snprintf(metric, b_len + 32, "%s.swap", base);
	statsd_send_int(fd, metric, g->swap_used);

	free(base);
	free(metric);
}

static int groupsort(const void *a, const void *b)
{
	int i = ((struct condor_group *)a)->sort_order;
	int j = ((struct condor_group *)b)->sort_order;

	return i - j;
}

static void usage(const char *progname)
{
	fprintf(stderr,
"Usage: %s [-p PATH] [-c CGROUP] GRAPHITE_DEST\n\n"
"GRAPHITE_DEST is either host:port or just host with port defaulting to the\n"
"standard line-protocol port 2003\n\n"
"Options:\n\t-c CGROUP: condor cgroup name (default %s)\n"
"\t-p PATH: metric path prefix for graphite (default %s)\n\n"
"Flags:\n\t-d Debug mode: print metrics to screen and don't send to graphite\n"
"\t-t Use TCP connection instead of the default (UDP). All metrics will\n"
"\t      be sent in one connection instead of 1 packet per metric\n"
"\t-h show this help message\n\n",
	progname, cgroup_name, root_ns);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	char dest[128];
	char *port = "2003";
	char *p;
	int fd;
	int c;
	int conn_class = GRAPHITE_UDP;

	while ((c = getopt(argc, argv, "hdc:p:t")) != -1) {
		switch (c) {
		case 'd':
			debug = 1;
			graphite_debug = 1;
			break;
		case 'c':
			cgroup_name = optarg;
			break;
		case 'p':
			root_ns = optarg;
			break;
		case 'h':
			usage(argv[0]);
			break;
		case 't':
			conn_class = GRAPHITE_TCP;
			break;
		case '?':
			if (optopt == 'p' || optopt == 'c')
				fprintf (stderr,
					 "Option -%c requires an argument.\n",
					 optopt);
			else if (isprint(optopt))
				fprintf (stderr,
					 "Unknown option `-%c'.\n", optopt);
			else
				fprintf (stderr,
					"Unknown option character `\\x%x'.\n",
					optopt);
			return 1;
		default:
			abort();
		}
	}

	if(optind >= argc)
		usage(argv[0]);

	if((p = strchr(argv[optind], ':')) != NULL)	{
		size_t hlen = min(p - argv[optind], sizeof(dest)-1);
		port = p + 1;
		strncpy(dest, argv[optind], hlen);
		*(dest + hlen) = '\0';
	} else {
		strncpy(dest, argv[optind], sizeof(dest) - 1);
	}

	gethostname(hostname, sizeof(hostname));

	graphite_init(conn_class);

	fd = graphite_connect(dest, port);

	get_condor_cgroups("cpu", cgroup_name);

	if(n_groups == 0)	{
		if(debug) {
			fputs("No condor cgroups groups found...exiting\n",
			      stderr);
		}
		return 0;
	}

	qsort(groups, n_groups, sizeof(*groups), groupsort);

	get_cgroup_statistics();

	for(int i = 0; i < n_groups; i++)	{
		send_group_metrics(&groups[i], fd);
	}
	graphite_close(fd);
	free(groups);
	return 0;
}
