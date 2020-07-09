#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <getopt.h>
#include <unistd.h>
#include <inttypes.h>

#include "graphite.h"
#include "statsd.h"

#include "cgroup.h"
#include "metrics.h"
#include "util.h"

static char hostname[256];
static char *root_ns = "htcondor.cgroups";

static inline int min(int a, int b) { return (a < b) ? a : b; }

enum backend {
	GRAPHITE,
	STATSD,
};

static void usage(const char *progname, enum backend b)
{
	if(b == GRAPHITE) {
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
		progname, default_cgroup_name, root_ns);

	} else {
		fprintf(stderr,
"Usage: %s [-p PATH] [-c CGROUP] STATSD_HOST\n\n"
"STATSD_HOST is either host:port or just host with port defaulting to the\n"
"standard statsd port 8125\n\n"
"Options:\n\t-c CGROUP: condor cgroup name (default %s)\n"
"\t-p PATH: metric path prefix for statsd (default %s)\n\n"
"Flags:\n\t-d Debug mode: print metrics to screen and don't send to statsd\n"
"\t-h show this help message\n\n",
		progname, default_cgroup_name, root_ns);
	}
	exit(EXIT_FAILURE);
}

int main(int argc, char *argv[])
{
	char dest[128];
	const char *cgroup_name = default_cgroup_name;
	char *port = "2003";
	char *p;
	int fd;
	int c;
	int conn_class = GRAPHITE_UDP;
	enum backend mode;

	mode = strstr(argv[0], "statsd") ? STATSD : GRAPHITE;

	while ((c = getopt(argc, argv, (mode == GRAPHITE) ?
					"hdc:p:t" : "hdc:p:")) != -1) {
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
			usage(argv[0], mode);
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
		usage(argv[0], mode);

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
	if(mode == GRAPHITE)	{
		fd = graphite_connect(dest, port);
	} else {
		fd = statsd_connect(dest, port);
	}

	read_condor_cgroup_info(cgroup_name);


	if(groups_empty())	{
		if(debug) {
			fputs("No condor cgroups groups found...exiting\n",
			      stderr);
		}
		return 0;
	}

	for_each_group(g)	{
		send_group_metrics(g, hostname, root_ns, fd,
			(mode == GRAPHITE) ?
			&graphite_send_uint : &statsd_send_uint
		);
	}

	if(mode == GRAPHITE) {
		graphite_close(fd);
	} else {
		statsd_close(fd);
	}
	cleanup_groups();
	return 0;
}
