#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "graphite.h"
#include "cgroup.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

static char hostname[256];
static const char * const root_ns = "htcondor.cgroups";

static void send_group_metrics(struct condor_group *g, int fd)
{
	char base[256];
	char metric[sizeof(base) + 64];
	snprintf(base, 256, "%s.%s.%s", root_ns, hostname, g->slot_name);

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


int main(int argc, char *argv[])
{
	char dest[128];
	char *port = "2003";
	char *p;
	int fd;

	if(argc < 2)	{
		fprintf(stderr,
			"Usage: %s GRAPHITE_DEST\n\n"
			"Where GRAPHITE_DEST is either host:port or just host, where port\n"
			"defaults to the line-protocol port 2003\n\n",
			argv[0]
		);
		return 1;
	}

	gethostname(hostname, sizeof(hostname));

	if((p = strchr(argv[1], ':')) != NULL)	{
		size_t hlen = p - argv[1];
		port = p + 1;
		strncpy(dest, argv[1], MIN(hlen, sizeof(dest)));
	} else {
		strncpy(dest, argv[1], sizeof(dest));
	}

	fd = graphite_connect(dest, port);


	get_condor_cgroups("memory");

	if(n_groups == 0)	{
		fputs("No condor cgroups groups found...exiting\n", stderr);
		return 1;
	}

	get_cgroup_statistics();

	graphite_init();
	for(int i = 0; i < n_groups; i++)	{
		send_group_metrics(&groups[i], fd);
	}

	close(fd);
	return 0;
}

