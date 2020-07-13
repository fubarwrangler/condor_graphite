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

typedef void (*read_fn)(const char *, struct condor_group *);

/* Utility structures used only in this file's functions */
struct found_groups {
	char *name;
	struct found_groups *next;
};

struct cg_stat {
	char *name, *value;
};

/* Controllers to read and the functions to call on each path prototyped and
 * defined here -- a static array of controllers we can iterate through below
 */
void read_cpu_group(const char *path, struct condor_group *g);
void read_memory_group(const char *path, struct condor_group *g);

struct controller {
	char *mount;		/* to be filled out when parsing cgroup tree */
	const char *name;
	read_fn populate;	/* these two below ... */
} controllers [] = {
	{ .name = "cpu",	.populate = read_cpu_group},
	{ .name = "memory",	.populate = read_memory_group},
};

#define NUM_CONTROLLERS sizeof(controllers)/sizeof(*controllers)

#define for_each_controller(c) \
for(struct controller *c = controllers; c < (controllers + NUM_CONTROLLERS); ++c)

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

/* Transform a slot-id string into an sortable integer, if slots are
 * partitionable in condor, take the ids N_M and pack them into the upper and
 * lower 2 bytes of a 32-bit int NNMM
 */
static uint32_t get_slot_number(const char *slot)
{
	uint32_t a, b;
	uint32_t n = 0;


	if(sscanf(slot, "slot%u_%u", &a, &b) == 2)	{
		n = (a & 0x0000ffff) << 16 | (b & 0x0000ffff);
	} else if (sscanf(slot, "slot%u", &a) == 1)	{
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



/**
 * Iterate through group-array without exposing underlying structure
 * Called via the macro for_each_group() in this file's header
 *
 * Return: true = valid group next, false = out of groups
 */
bool __group_for_each(struct condor_group **g)
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


void cleanup_groups()
{
	n_groups = 0;
	if(groups)
		free(groups);
}

/* Iterate through stat-file represented by @path, reading lines like:
 * "key_name value" (one space separating key / value)
 * and return a pointer to a stat-structure with the key/values in the fields
 *
 * NOTE: Call multiple times until NULL is returned to finish parsing one file
 */
struct cg_stat *read_stats(const char *path)

{
	static FILE *fp = NULL;
	static struct cg_stat s;
	static char linebuf[128] = {0};


	if(fp == NULL)	{
		if((fp = fopen(path, "r")) == NULL)
			log_exit("Error opening %s", path);
	}

	if(fgets(linebuf, sizeof(linebuf), fp) != NULL)	{
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
			return &s;
		}
	}

	if(feof(fp))	{
		fclose(fp);
		fp = NULL;
	} else {
		log_exit("Error occured reading %s", path);
	}
	return NULL;
}

/* Read a single number from a file at @path and return it as an integer */
uint64_t read_num(const char *path)
{
	FILE *fp = fopen(path, "r");
	char buf[64];
	if(fp == NULL)
		log_exit("Error opening %s", path);
	fgets(buf, sizeof(buf), fp);
	if(ferror(fp))
		log_exit("Error reading %s", path);
	fclose(fp);
	return parse_num(buf);
}

void read_memory_group(const char *path, struct condor_group *g)	{
	struct cg_stat *s;
	struct stat st;
	while((s = read_stats(join_path(path, "memory.stat"))) != NULL)	{
		if(0 == strcmp(s->name, "total_rss"))	{
			g->rss_used = parse_num(s->value);
		} else if (0 == strcmp(s->name, "total_swap")) {
			g->swap_used = parse_num(s->value);
		} else if (0 == strcmp(s->name, "total_cache")) {
			g->cache_used = parse_num(s->value);
		}
	}
	if(stat(path, &st) != 0)
		log_exit("Error calling stat() on %d", path);
	g->start_time = st.st_ctime;
	g->mem_limit = read_num(join_path(path, "memory.soft_limit_in_bytes"));
}

void read_cpu_group(const char *path, struct condor_group *g)	{
	long int hz = sysconf(_SC_CLK_TCK);

	struct cg_stat *s;
	while((s = read_stats(join_path(path, "cpuacct.stat"))) != NULL)	{
		if(0 == strcmp(s->name, "user"))	{
			g->user_cpu_usage = parse_num(s->value);
		} else if (0 == strcmp(s->name, "system")) {
			g->sys_cpu_usage = parse_num(s->value);
		}
	}

	/* Divide by HZ from _SC_CLK_TCK to get usage in seconds */
	g->user_cpu_usage /= hz;
	g->sys_cpu_usage /= hz;
	g->cpu_shares = read_num(join_path(path, "cpu.shares"));
	g->num_procs = count_newlines(join_path(path, "cgroup.procs"));
	g->num_tasks = count_newlines(join_path(path, "tasks"));
}

void init_controller_paths(const char *path, struct found_groups **ccg)
{
	FILE *fp;
	DIR *dir;
	struct mntent *m;
	struct dirent *d;

	*ccg = NULL;

	// Find cgroup-labeled mounts points in /proc/mounts to fill into the
	// struct controller .mount member
	if(NULL == (fp = fopen("/proc/mounts", "r")))	{
		fprintf(stderr, "Error opening /proc/mounts\n");
		exit(EXIT_FAILURE);
	}

	while( (m = getmntent(fp)) != NULL)	{
		if(strcmp(m->mnt_type, "cgroup") != 0)
			continue;
		// Find mount options with "controller"-name
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

	// Create quick linked-list of per-slot-named cgroups by going through
	// the first controller's subdirectory named <mount>/@path/
	// NOTE: Assumption here is that subdirs are named the same between
	//       multiple controllers (if a job exits between the dir-scan and
	//       reading the data there could be a race / error...
	struct found_groups **cgitr = ccg;
	struct controller *c = &controllers[0];

	dir = opendir(c->mount);
	if(dir == NULL)
		log_exit("Cannot open directory: %s", c->mount);
	while(dir != NULL && (d = readdir(dir)) != NULL)	{
		if(d->d_name[0] == '.')
			continue;
#if !defined(DT_DIR) || !defined(_DIRENT_HAVE_D_TYPE)
#warning \
Fallback to using stat() to determine directory type, please define _BSD_SOURCE \
SEE man 3 readdir for more details on _DIRENT_HAVE_D_TYPE
		struct stat st;
		if(lstat(d->d_name, &st) != 0)
			log_exit("Error calling lstat() on %s", d->d_name);
		if(S_ISDIR(st.st_mode)) {
#else
		if(d->d_type == DT_DIR) {
#endif

			*cgitr = xcalloc(sizeof(struct found_groups));
			(*cgitr)->next = NULL;
			(*cgitr)->name = xstrdup(d->d_name);
			cgitr = &(*cgitr)->next;
		}
	}
	if(*ccg == NULL)
		log_exit("No %s cgroups found", path);
	closedir(dir);

}

void read_condor_cgroup_info(const char *cg_name)
{
	struct found_groups *cgroup, *c;
	struct condor_group *g;
	init_controller_paths(cg_name, &cgroup);
	c = cgroup;

	while(c)	{
		struct found_groups *tmp;
		if(NULL == (groups = realloc(groups,
			(1 + n_groups) * sizeof(struct condor_group))) ) {
			fputs("!Realloc error on group struct", stderr);
			exit(ENOMEM);
		}
		g = &groups[n_groups++];
		memset(g, 0, sizeof(struct condor_group));

		extract_slot_name(g->slot_name, c->name);
		g->sort_order = get_slot_number(g->slot_name);
		for_each_controller(ctrl)	{
			char *full_path = xstrdup(join_path(ctrl->mount, c->name));
			ctrl->populate(full_path, g);
			free(full_path);
		}
		// clean up after ourselves as this is the only time through the list
		tmp = c->next;
		free(c->name);
		free(c);
		c = tmp;
	}

	// sort by slot-id
	qsort(groups, n_groups, sizeof(*groups), groupsort);

	// clean up mounts for good measure
	for_each_controller(c)
		free(c->mount);
}



#ifdef _DBG_CGROUP
int main(void)
{
	read_condor_cgroup_info("htcondor");
	for_each_group(group)
		printf("%s %x %lu\n", group->slot_name, group->sort_order, group->rss_used);
}
#endif
