#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <assert.h>
#include <mntent.h>
#include <sys/types.h>
#include <dirent.h>

#include "cgroup.h"
#include "util.h"

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

static int groupsort(const void *a, const void *b)
{
	int i = ((struct condor_group *)a)->sort_order;
	int j = ((struct condor_group *)b)->sort_order;

	return i - j;
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
static int count_newlines(const char *path)
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

struct cgroup_stat {
	char *name, *value;
};

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

void read_stats(const char *path, struct condor_group *g,
		void (*pop_fn)(struct cgroup_stat *, struct condor_group *))

{
	FILE *fp = fopen(path, "r");
	char linebuf[128] = {0};
	struct cgroup_stat s;

	if(fp == NULL)
		log_exit("Error opening %s", path);

	while(NULL != fgets(linebuf, sizeof(linebuf), fp))	{
		char *p = linebuf;
		s.name = p;
		// Count up to first space
		while(*++p != ' ')
			;
		// and replace with \0 and arange pointers in stat such
		// that key,value are at right place in buffer
		if(*p == ' ' && *(p + 1) != '\0')	{
			*p++ = '\0';
			s.value = p;
			pop_fn(&s, g);
		}
	}
	fclose(fp);
}

uint64_t read_num(const char *path)
{
	FILE *fp = fopen(path, "r");
	char buf[64];
	if(fp == NULL) log_exit("Error opening %s", path);
	fgets(buf, sizeof(buf), fp);
	if(ferror(fp)) log_exit("Error reading %s", path);
	fclose(fp);
	return parse_num(buf);
}

void read_memory_group(const char *path, struct condor_group *g)	{
	read_stats(join_path(path, "memory.stat"), g, &_populate_memory_stat);
}

void read_cpu_group(const char *path, struct condor_group *g)	{
	long int hz = sysconf(_SC_CLK_TCK);

	read_stats(join_path(path, "cpuacct.stat"), g, &_populate_memory_stat);
	/* Divide by HZ from _SC_CLK_TCK to get usage in seconds */
	g->user_cpu_usage /= hz;
	g->sys_cpu_usage /= hz;
	g->cpu_shares = read_num(join_path(path, "cpu.shared"));
	g->num_procs = count_newlines(join_path(path, "cgroup.procs"));
	g->num_tasks = count_newlines(join_path(path, "tasks"));
}


typedef void (*read_fn)(const char *, struct condor_group *);

struct controller {
	const char *name;
	char *mount;
	read_fn populate;
} controllers [] = {
	{ .name = "cpu",	.populate = read_cpu_group},
	{ .name = "memory",	.populate = read_memory_group},
};

#define NUM_CONTROLLERS sizeof(controllers)/sizeof(*controllers)

struct found_groups {
	const char *name;
	struct found_groups *next;
};

#define for_each_controller(c) \
for(struct controller *c = controllers; c < (controllers + NUM_CONTROLLERS); ++c)

#define for_each_cg(cgp, cgs)	\
for(struct found_groups *cgp = cgs; cgp != NULL; cgp = cgp->next)

void init_controller_paths(const char *path, struct found_groups **ccg)
{
	FILE *fp;
	DIR *dir;
	struct mntent *m;
	struct dirent *d;

	if(NULL == (fp = fopen("/proc/mounts", "r")))	{
		fprintf(stderr, "Error opening /proc/mounts\n");
		exit(EXIT_FAILURE);
	}

	while( (m = getmntent(fp)) != NULL)	{
		if(strcmp(m->mnt_type, "cgroup") != 0)
			continue;
		for_each_controller(c) {
			if(hasmntopt(m, c->name))	{
				c->mount = xcalloc(strlen(m->mnt_dir) + strlen(path) + 2);
				sprintf(c->mount, "%s/%s", m->mnt_dir, path);
			}
		}
	}
	fclose(fp);

	for_each_controller(c)
		if(c->mount == NULL)
			log_exit("Error reading all controller cgroups!");

	struct found_groups **cgitr = ccg;
	struct controller *c = &controllers[0];

	dir = opendir(c->mount);
	while(dir != NULL && (d = readdir(dir)) != NULL)	{
		if(d->d_type == DT_DIR && d->d_name[0] != '.') {
			*cgitr = xcalloc(sizeof(struct found_groups));
			(*cgitr)->next = NULL;
			(*cgitr)->name = xstrdup(d->d_name);
			cgitr = &(*cgitr)->next;
		}
	}
	closedir(dir);

}

void read_condor_cgroup_info(void)
{
	struct found_groups *cg;
	struct condor_group *g;
	init_controller_paths("htcondor", &cg);
	for_each_cg(c, cg)	{
		for_each_controller(ctrl)	{
			if(NULL == (groups = realloc(groups,
				(1 + n_groups) * sizeof(struct condor_group))) ) {
				fputs("!Realloc error on group struct", stderr);
				exit(ENOMEM);
			}
			g = &groups[n_groups++];
			memset(g, 0, sizeof(struct condor_group));
			char *full_path = xstrdup(join_path(ctrl->mount, c->name));
			ctrl->populate(full_path, g);
			free(full_path);
		}
	}
}



#define _DBG_CGROUP
#ifdef _DBG_CGROUP


int main(void)
{
	struct condor_cgroups *cg;


	init_controller_paths("htcondor", &cg);
	for(struct condor_cgroups *p = cg; p != NULL; p = p->next)	{
		printf("%s\n", p->name);
	}
	return 0;
}

#endif
