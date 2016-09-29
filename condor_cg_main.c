#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <inttypes.h>

#if USE_GRAPHITE
#include "graphite.h"
#elif USE_STATSD
#include "statsd.h"
#endif

#include "cgroup.h"
#include "util.h"

static char hostname[256];
static char *root_ns = "htcondor.cgroups";

static inline int min(int a, int b) { return (a < b) ? a : b; }

static void usage(const char *progname)
{
	fprintf(stderr,
#ifdef USE_GRAPHITE
"Usage: %s [-p PATH] [-c CGROUP] GRAPHITE_DEST\n\n"
"GRAPHITE_DEST is either host:port or just host with port defaulting to the\n"
"standard line-protocol port 2003\n\n"
"Options:\n\t-c CGROUP: condor cgroup name (default %s)\n"
"\t-p PATH: metric path prefix for graphite (default %s)\n\n"
"Flags:\n\t-d Debug mode: print metrics to screen and don't send to graphite\n"
"\t-t Use TCP connection instead of the default (UDP). All metrics will\n"
"\t      be sent in one connection instead of 1 packet per metric\n"
"\t-h show this help message\n\n",
#elif USE_STATSD /* STATSD */
"Usage: %s [-p PATH] [-c CGROUP] STATSD_HOST\n\n"
"STATSD_HOST is either host:port or just host with port defaulting to the\n"
"standard statsd port 8125\n\n"
"Options:\n\t-c CGROUP: condor cgroup name (default %s)\n"
"\t-p PATH: metric path prefix for statsd (default %s)\n\n"
"Flags:\n\t-d Debug mode: print metrics to screen and don't send to statsd\n"
"\t-h show this help message\n\n",
#endif
	progname, default_cgroup_name, root_ns);
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	char dest[128];
	const char *cgroup_name = default_cgroup_name;
#ifdef USE_GRAPHITE
	char *port = "2003";
#elif USE_STATSD
	char *port = "8125";
#endif
	char *p;
	int fd;
	int c;
#if USE_GRAPHITE
	int conn_class = GRAPHITE_UDP;

	while ((c = getopt(argc, argv, "hdc:p:t")) != -1) {
#elif USE_STATSD
	while ((c = getopt(argc, argv, "hdc:p:")) != -1) {
#endif
		switch (c) {
		case 'd':
			debug = 1;
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
#ifdef USE_GRAPHITE
		case 't':
			conn_class = GRAPHITE_TCP;
			break;
#endif
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
#ifdef USE_GRAPHITE
	graphite_init(conn_class);
	fd = graphite_connect(dest, port);
#elif USE_STATSD
	fd = statsd_connect(dest, port);
#endif

	get_condor_cgroups("cpu", cgroup_name);

	if(n_groups == 0)	{
		if(debug) {
			fputs("No condor cgroups groups found...exiting\n",
			      stderr);
		}
		return 0;
	}
	get_cgroup_statistics();

	qsort(groups, n_groups, sizeof(*groups), groupsort);

	for(int i = 0; i < n_groups; i++)	{
		send_group_metrics(&groups[i], hostname, root_ns, fd,
#ifdef USE_GRAPHITE
				   &graphite_send_uint);
#elif USE_STATSD
				   &statsd_send_uint);
#endif
	}

#ifdef USE_GRAPHITE
	graphite_close(fd);
#elif USE_STATSD
	statsd_close(fd);
#endif
	free(groups);
	return 0;
}
