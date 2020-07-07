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
#include <mntent.h>

#include "cgroup.h"

/* Data structure is just an array of group structures */
static struct condor_group *groups = NULL;

/* Keep track of size of above */
static int n_groups = 0;

const char *default_cgroup_name = "htcondor";

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

	if(*p != '@')	{
		fprintf(stderr, "Invalid cgroup-name, should have @-sign!\n");
		exit(EXIT_FAILURE);
	}
	/* Really!? This is the way to access a structure-member's size? */
	strncpy(slot_name, (p - i - 1),
		sizeof(((struct condor_group *)0)->slot_name) - 1);
	*(slot_name + i + 1) = '\0';
}


static uint32_t get_slot_number(const char *slot)
{
	uint32_t a, b;
	uint32_t n = 0;

	if(sscanf(slot, "slot%u_%u", &a, &b) == 2)	{
		n = (a & 0x0000ffff) << 16 | (b & 0x0000ffff);
	} else if (sscanf(slot, "slot_%u", &a) != 1)	{
		n = (a & 0x0000ffff) << 16;
	}
	return n;
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
			sizeof(((struct condor_group *)0)->name) - 1);
		strncpy(g->root_path, info->full_path, root_len);

		*(g->root_path + root_len) = '\0';
		extract_slot_name(g->slot_name, info->path);
		g->sort_order = get_slot_number(g->slot_name);

		if(stat(info->full_path, &st) < 0)	{
			fprintf(stderr, "Error stat'ing cgroup %s: %s\n",
				info->full_path, strerror(errno)
			);
			exit(EXIT_FAILURE);
		}
		g->start_time = st.st_ctime;
		return 0;
	}
}


static int groupsort(const void *a, const void *b)
{
	int i = ((struct condor_group *)a)->sort_order;
	int j = ((struct condor_group *)b)->sort_order;

	return i - j;
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

	qsort(groups, n_groups, sizeof(struct condor_group), groupsort);

	return;

	fail_out:
		fprintf(stderr, "Error walking controller: %s for %s\n",
			controller, condor_cgroup);
		exit(EXIT_FAILURE);
}

/* Return true if no groups found */
bool groups_empty(void)
{
	return (n_groups == 0);
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

/**
 * Get the number of tasks / pids in a cgroup from appropriate files @path
 * works by counting newlines in @path, since PIDs/tasks are one-per-line
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

/**
 * Reads statistics from <@controller>.stats file under given cgroup @g:
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

/**
 * Iterate through group-array without exposing underlying structure
 * Call with address of pointer to groups @g initalized to NULL, then
 * on each subsequent call where it returns True, @g points to the next group
 *
 * Return: true = valid group next, false = out of groups
 */
bool group_for_each(struct condor_group **g)
{
	static int n;

	assert(g != NULL);

	if(*g == NULL)
		n = 0;

	if(n >= n_groups)	{
		return false;
	} else	{
		*g = &groups[n++];
		return true;
	}
}

static void _populate_memory_stat(struct cgroup_stat *s, struct condor_group *g)
{
	if(0 == strcmp(s->name, "total_rss"))	{
		g->rss_used = parse_num(s->value);
	} else if (0 == strcmp(s->name, "total_swap")) {
		g->swap_used = parse_num(s->value);
	} else if (0 == strcmp(s->name, "total_cache")) {
		g->cache_used = parse_num(s->value);
	}
}


static void _populate_cpu_stat(struct cgroup_stat *s, struct condor_group *g)
{
	if(0 == strcmp(s->name, "user"))	{
		g->user_cpu_usage = parse_num(s->value);
	} else if (0 == strcmp(s->name, "system")) {
		g->sys_cpu_usage = parse_num(s->value);
	}
}

void get_cgroup_statistics(const char *cgroup_name)
{
	char cgpath[sizeof(((struct condor_group *)0)->name) + 16] = {0};
	struct cgroup *c = NULL;

	struct cgroup_controller *cont = NULL;
	long int hz = sysconf(_SC_CLK_TCK);
	int ret;

	assert(groups != NULL);

	for(struct condor_group *g = NULL; group_for_each(&g); /*noop*/)	{

		snprintf(cgpath, sizeof(cgpath), "%s/%s", cgroup_name, g->name);
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
		get_controller_stats("memory", g, cgpath,
				     &_populate_memory_stat);

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

void cleanup_groups()
{
	n_groups = 0;
	if(groups)
		free(groups);
}

void _read_cpu_info(const char *path, struct condor_group *g)	{
	return ;
}

void _read_memory_info(const char *path, struct condor_group *g)	{
	return ;
}

struct _stats {
	const char *name;
	void (*_pop_fn)(const char *, struct condor_group *);
} defs[] = { {"memory", _read_memory_info}, {"cpu", _read_cpu_info} };





//#define _DBG_CGROUP
#ifdef _DBG_CGROUP


int main(void)
{
	FILE *fp = fopen("/etc/mtab", "r");
	char *controllers[] = {"memory", "cpu", NULL};
	struct mntent *m;
	while( (m = getmntent(fp)) != NULL)	{
		printf("%s %s (%s)\n", m->mnt_type, m->mnt_dir, m->mnt_opts);
		for(char **p = controllers; *p; ++p)	{
			if(hasmntopt(m, *p))	{
				printf("\t-> has %s\n", *p);
				break;
			}
		}
	}
	fclose(fp);
	return 0;
}

#endif
