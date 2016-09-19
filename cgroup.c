#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <libcgroup.h>
#include <assert.h>

#include "cgroup.h"

struct condor_group *groups = NULL;
int n_groups = 0;


/*
 * Get slot name from @cgroup_name under condor/ folder.
 * format: "components_in_scratch_path_SLOTNAME@host"
 * We scan for first "slot" string, then count up to first "@"-sign, then
 * copy bytes between into buffer @slot_name as slot's name
 *
 * WARNING: This function assumes format of cgroup name created by condor!
 */
static void extract_slot_name(char *slot_name, const char *cgroup_name)
{
	size_t i = 0;
	char *p = strstr(cgroup_name, "slot");

	if(p == NULL)	{
		fprintf(stderr, "Invalid cgroup-name, needs 'slot' in it!\n");
		exit(EXIT_FAILURE);
	}

	/* Run up to first '@' sign */
	while(++p && *p != '@' && *p != '\0')
		i++;

	if(*p == '\0')	{
		fprintf(stderr, "Invalid cgroup-name, should have @-sign!\n");
		exit(EXIT_FAILURE);
	}
	/* Really!? This is the way to access a structure-member's size? */
	strncpy(slot_name, (p - i - 1),
		sizeof(((struct condor_group *)0)->slot_name));
	*(slot_name + i + 1) = '\0';
}


/* TODO: These (find & add) could be made more efficient with a hash or tree */
static struct condor_group *find_group(const char *name)
{
	for(int i = 0; i < n_groups; i++)	{
		if(!strcmp(groups[i].name, name))
			return &groups[i];
	}
	return NULL;
}

static int add_group(struct cgroup_file_info *info)
{
	struct condor_group *g;
	size_t root_len;
	if(NULL != (g = find_group(info->path))) {
		printf("Found already %s...\n", info->path);
		return 0;
	} else {
		struct stat st;

		if(NULL == (groups = realloc(groups,
			(1 + n_groups) * sizeof(struct condor_group))) ) {
			fputs("!Realloc error on group struct", stderr);
			exit(ENOMEM);
		}
		g = &groups[n_groups++];
		memset(g, 0, sizeof(struct condor_group));

		root_len = strlen(info->full_path) - strlen(info->path);

		strncpy(g->name, info->path,
			sizeof(((struct condor_group *)0)->name));
		strncpy(g->root_path, info->full_path,
			sizeof(((struct condor_group *)0)->root_path));

		*(g->root_path + root_len) = '\0';
		extract_slot_name(g->slot_name, info->path);
		stat(info->full_path, &st);
		g->start_time = st.st_ctime;
		return 0;
	}
}


/* Walk through the children of the "condor" cgroup under one controller to
 * get the names of the current slot-cgroups and fill out group structure
 */
void get_condor_cgroups(const char *controller, const char *condor_cgroup)
{
	struct cgroup_file_info info;
	void *handle = NULL;
	int level = -1;
	int ret;

	if((ret = cgroup_init()) != 0) {
		fprintf(stderr, "Error initalizing libcgroup: %s\n",
			cgroup_strerror(ret));
		exit(EXIT_FAILURE);
	}

	ret = cgroup_walk_tree_begin(controller, condor_cgroup, 1,
				     &handle, &info, &level);
	if(ret != 0)
		goto fail_out;
	if(info.type == CGROUP_FILE_TYPE_DIR && info.depth == 1)
		add_group(&info);

	while (ECGEOF != (ret = cgroup_walk_tree_next(0, &handle,
							&info, level))) {
		if(ret != 0)
			goto fail_out;
		if(info.type == CGROUP_FILE_TYPE_DIR && info.depth == 1)
			add_group(&info);
	}

	cgroup_walk_tree_end(&handle);
	return;

	fail_out:
		fprintf(stderr, "Error walking controller: %s for %s\n",
			controller, condor_cgroup);
		exit(EXIT_FAILURE);
}

/* Get number fron string in a safe way (exit on failure) */
static uint64_t parse_num(const char *str)
{
	char *p;
	unsigned long int n;

	errno = 0;
	n = strtoul(str, &p, 10);
	if(errno != 0) {
		fprintf(stderr, "Error converting '%s' to integer\n", str);
		exit(EXIT_FAILURE);
	}
	return (uint64_t)n;
}

/* Get the number of tasks / pids in a cgroup from appropriate files @path
 * works by counting newlines in @path, since PIDs/tasks are on-per-line
 */
static int read_num_tasks(const char *path)
{
	FILE *fp;
	int n = 0;
	char c;

	if(NULL == (fp = fopen(path, "r")))	{
		fprintf(stderr, "Error opening %s", path);
		exit(EXIT_FAILURE);
	}

	while((c = fgetc(fp)) != EOF)	{
		if(c == '\n')
			++n;
	}

	fclose(fp);
	return n;
}

/* Get the number of tasks & pids in a cgroup @g */
static void get_procs_in_group(struct condor_group *g)
{
	char path[sizeof(g->name) + sizeof(g->root_path) + 64];

	snprintf(path, sizeof(path), "%s%s/cgroup.procs",
		 g->root_path, g->name);
	g->num_procs = read_num_tasks(path);

	snprintf(path, sizeof(path), "%s%s/tasks", g->root_path, g->name);
	g->num_tasks = read_num_tasks(path);
}

/* Get a uint64_t type statistics from cgroup via libcgroup methods */
inline static void _set_cgroup_int(struct cgroup_controller *c,
				const char *name, uint64_t *u)
{
	int rv = cgroup_get_value_uint64(c, name, u);
	if(rv != 0) {
		fprintf(stderr, "Error reading val\n");
		exit(EXIT_FAILURE);
	}
}

/* Reads statistics from <@controller>.stats file under given cgroup @g:
 * @path is full-path of cgroup under main location
 * @pop_fn() takes stat and group arg, looks for key-value pairs to populate
 *           from the stat data, called once for each stat found
 */
static void get_controller_stats(const char *controller,
				 struct condor_group *g,
				 const char *path,
				 void (*_pop_fn)(struct cgroup_stat *,
						 struct condor_group *))
{
	struct cgroup_stat stat;
	void *handle;
	int rv;

	rv = cgroup_read_stats_begin(controller, path, &handle, &stat);
	if(rv != 0 && rv != ECGEOF)
		goto fail_out;

	(*_pop_fn)(&stat, g);
	while((rv = cgroup_read_stats_next(&handle, &stat)) != ECGEOF)	{
		if(rv != 0)
			goto fail_out;
		(*_pop_fn)(&stat, g);
	}
	cgroup_read_stats_end(&handle);
	return;

	fail_out:
		fprintf(stderr, "Error reading stats for %s\n", controller);
		exit(EXIT_FAILURE);

}


static void _populate_cpu_stat(struct cgroup_stat *s, struct condor_group *g)
{
	if(0 == strcmp(s->name, "user"))	{
		g->user_cpu_usage = parse_num(s->value);
	} else if (0 == strcmp(s->name, "system")) {
		g->sys_cpu_usage = parse_num(s->value);
	}
}

void get_cgroup_statistics()
{
	char cgpath[sizeof(((struct condor_group *)0)->name) + 16] = {0};
	struct cgroup *c = NULL;
	struct condor_group *g = NULL;
	struct cgroup_controller *cont = NULL;
	long int hz = sysconf(_SC_CLK_TCK);
	int ret;

	assert(groups != NULL);

	for(int i = 0; i < n_groups; i++)	{
		g = &groups[i];
		snprintf(cgpath, sizeof(cgpath), "condor/%s", g->name);
		if((c = cgroup_new_cgroup(cgpath)) == NULL)	{
			fprintf(stderr, "CGroup error allocating %s\n",cgpath);
			exit(EXIT_FAILURE);
		}
		if((ret = cgroup_get_cgroup(c)) != 0)	{
			fprintf(stderr, "Error getting %s data: %s",
					g->name, cgroup_strerror(ret));
			exit(EXIT_FAILURE);
		}
		/* Memory stats */
		cont = cgroup_get_controller(c, "memory");
		_set_cgroup_int(cont, "memory.usage_in_bytes", &g->rss_used);

		/* NOTE: this param is sum of swap+rss */
		_set_cgroup_int(cont, "memory.memsw.usage_in_bytes",
				&g->swap_used);

		/* NOTE: underflow! Probably because values are read at
		 * slightly different times by libcgroup, so we sanity check.
		 */
		if(g->swap_used <= g->rss_used)
			g->swap_used = 0;
		else
			g->swap_used -= g->rss_used;

		/* CPU Shares */
		cont = cgroup_get_controller(c, "cpu");
		_set_cgroup_int(cont, "cpu.shares", &g->cpu_shares);

		/* CPU Usage stats */
		get_controller_stats("cpuacct", g, cgpath,
				     &_populate_cpu_stat);
		/* Divide by HZ from _SC_CLK_TCK to get usage in seconds */
		g->user_cpu_usage /= hz;
		g->sys_cpu_usage /= hz;

		/* Count processes and threads in group */
		get_procs_in_group(g);

		cgroup_free(&c);
	}
}
//#define _DBG_CGROUP
#ifdef _DBG_CGROUP
void print_groups(void)
{
	struct condor_group *g = groups;

	for(int i = 0; i < n_groups; i++, g = &groups[i])	{
		printf("Group %d (created %ld): %s\n",
		       i, g->start_time, g->name);
		printf("\tSlotid: %s\n", g->slot_name);
		printf("\tRSS: %lu\n\tSWAP: %lu\n", g->rss_used, g->swap_used);
		printf("\tProcesses (threads): %d (%d)\n",
			g->num_procs, g->num_tasks);
		printf("\tCPU usage (%lu share): %lu user / %lu sys\n",
			g->cpu_shares, g->user_cpu_usage, g->sys_cpu_usage);
	}
}
#define CONDOR_GROUP "condor"

int main(
	int __attribute__((unused)) argc,
	char __attribute__((unused)) *argv[]
)
{
	get_condor_cgroups("memory", CONDOR_GROUP);
	if(n_groups == 0)	{
		fputs("No condor " CONDOR_GROUP " groups found\n", stderr);
		return 1;
	}
	get_cgroup_statistics();
	print_groups();
	free(groups);
	return 0;
}
#endif
