#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

#include "graphite.h"
#include "cgroup.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static char hostname[256];
static char *root_ns = "htcondor.cgroups";
static char *cgroup_name = "condor";
static int debug = 0;

static void send_group_metrics(struct condor_group *g, int fd)
{
	char base[256];
	char metric[sizeof(base) + 64];
	char sanitized_host[sizeof(hostname)];
	char *p = sanitized_host;

	strcpy(sanitized_host, hostname);
	do {
		if(*p == '.') *p = '_';
	} while(*p++);

	snprintf(base, sizeof(base), "%s.%s.%s",
		 root_ns, sanitized_host, g->slot_name);

	sprintf(metric, "%s.cpu_shares", base);
	graphite_send_uint(fd, metric, g->cpu_shares);

	sprintf(metric, "%s.tasks", base);
	graphite_send_uint(fd, metric, g->num_tasks);

	sprintf(metric, "%s.procs", base);
	graphite_send_uint(fd, metric, g->num_procs);

	sprintf(metric, "%s.cpu_user", base);
	graphite_send_uint(fd, metric, g->user_cpu_usage);

	sprintf(metric, "%s.cpu_sys", base);
	graphite_send_uint(fd, metric, g->sys_cpu_usage);

	sprintf(metric, "%s.rss", base);
	graphite_send_uint(fd, metric, g->rss_used);

	sprintf(metric, "%s.swap", base);
	graphite_send_uint(fd, metric, g->swap_used);
}

static int groupsort(const void *a, const void *b)
{
	return strcmp(((struct condor_group *)a)->slot_name,
			((struct condor_group *)b)->slot_name);
}

static void usage(const char *progname)
{
	fprintf(stderr,
"Usage: %s [-p PATH] [-c CGROUP] GRAPHITE_DEST\n\n"
"GRAPHITE_DEST is either host:port or just host with port defaulting to the\n"
"standard line-protocol port 2003\n\n"
"Options:\n\t-c CGROUP: condor cgroup name (default %s)\n"
"\t-p PATH: metric path prefix for graphite (default %s)\n"
"\t-h show this usage help\n\n",
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

	while ((c = getopt(argc, argv, "hdc:p:")) != -1) {
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
		size_t hlen = p - argv[1];
		port = p + 1;
		strncpy(dest, argv[optind], MIN(hlen, sizeof(dest)));
	} else {
		strncpy(dest, argv[optind], sizeof(dest));
	}

	gethostname(hostname, sizeof(hostname));

	fd = graphite_connect(dest, port);


	get_condor_cgroups("cpu", "condor");

	if(n_groups == 0)	{
		fputs("No condor cgroups groups found...exiting\n", stderr);
		return 1;
	}

	qsort(groups, n_groups, sizeof(*groups), groupsort);

	get_cgroup_statistics();

	graphite_init();
	for(int i = 0; i < n_groups; i++)	{
		send_group_metrics(&groups[i], fd);
	}
	close(fd);
	free(groups);
	return 0;
}
