#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <linux/limits.h>
#include <libcgroup.h>
#include <assert.h>

#define CONDOR_ROOT_GROUP "condor"

struct condor_group {
	char name[192];
	char root_path[256];
	uint32_t num_procs;
	uint32_t num_tasks;
	uint32_t cpu_shares;
	uint64_t user_cpu_usage;
	uint64_t sys_cpu_usage;
	uint64_t rss_used;
	uint64_t swap_used;
} *groups = NULL;
int n_groups = 0;

/* TODO: These (find & add) could be made more efficient with a hash or tree */
struct condor_group *find_group(const char *name)
{
	for(int i = 0; i < n_groups; i++)	{
		if(!strcmp(groups[i].name, name))
			return &groups[i];
	}
	return NULL;
}

int add_group(struct cgroup_file_info *info, struct condor_group **grp)
{
	size_t root_len;
	if(NULL != (*grp = find_group(info->path)))	{
		printf("Found already %s...\n", info->path);
		return 0;
	} else {
		printf("Adding group (%d) %s -- %s\n", n_groups + 1, info->path, info->full_path);
		if(NULL == (groups = realloc(groups,
				(1 + n_groups) * sizeof(struct condor_group))))
			exit(ENOMEM);
		*grp = &groups[n_groups++];
		root_len = strlen(info->full_path) - strlen(info->path);

		/* Really!? This is the way to access a structure-member's size? */
		strncpy((*grp)->name, info->path, sizeof(((struct condor_group *)0)->name));
		strncpy((*grp)->root_path, info->full_path, sizeof(((struct condor_group *)0)->root_path));

		*((*grp)->root_path + root_len) = '\0';
		return 0;
	}
}

/* Walk through the children of the "condor" cgroup under one controller to
 * get the names of the current slot-cgroups
 *
 * WARNING: This function uses static storage, not threadsafe!
 */
void get_condor_cgroups(void)
{
	static void *handle = NULL;
	static int level = -1;
	struct condor_group *g;
	struct cgroup_file_info info;
	const char *controller = "memory";
	int ret;

	if(handle == NULL)	{
		ret = cgroup_walk_tree_begin(controller, CONDOR_ROOT_GROUP, 1,
					     &handle, &info, &level);
		if (ret != 0) {
			fprintf(stderr,
				"Error walking controller: %s", controller);
			exit(EXIT_FAILURE);
		}
		if(info.type == CGROUP_FILE_TYPE_DIR && info.depth == 1)
			add_group(&info, &g);
	}
	while ((ret = cgroup_walk_tree_next(0, &handle, &info, level)) != ECGEOF)	{
		if (ret != 0) {
			fprintf(stderr,
				"Error walking controller: %s", controller);
			exit(EXIT_FAILURE);
		}
		if(info.type == CGROUP_FILE_TYPE_DIR && info.depth == 1)
			add_group(&info, &g);
	}

	cgroup_walk_tree_end(&handle);
	handle = NULL;
}

uint64_t parse_num(const char *str)
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

/* Count the newlines in @path, since PIDs are on-per-line */
int read_num_tasks(const char *path)
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

void get_procs_in_group(struct condor_group *g)
{
	char path[sizeof(g->name) + sizeof(g->root_path) + 64];

	assert(path != NULL);

	snprintf(path, sizeof(path), "%s%s/cgroup.procs", g->root_path, g->name);
	printf("Reading path: %s\n", path);
	g->num_procs = read_num_tasks(path);

	snprintf(path, sizeof(path), "%s%s/tasks", g->root_path, g->name);
	printf("Reading path: %s\n", path);
	g->num_tasks = read_num_tasks(path);

	free(path);
}

void get_memory_stats(struct cgroup_controller *c, struct condor_group *g)
{
	int rv;
	uint64_t rss, swap;

	rv = cgroup_get_value_uint64(c, "memory.usage_in_bytes", &rss);
	if(rv != 0) { fprintf(stderr, "Error reading val\n"); exit(EXIT_FAILURE); }
	g->rss_used = rss;

	/* Returns sum of swap+rss */
	rv = cgroup_get_value_uint64(c, "memory.memsw.usage_in_bytes", &swap);
	if(rv != 0) { fprintf(stderr, "Error reading val\n"); exit(EXIT_FAILURE); }
	g->swap_used = swap - rss;

	get_procs_in_group(g);
}

void get_statistics()
{
	char foo[512];
	struct cgroup *c;
	struct condor_group *g = NULL;
	struct cgroup_controller *cont;
	int ret;

	assert(groups != NULL);

	for(int i = 0; i < n_groups; i++)	{
		g = &groups[i];
		snprintf(foo, sizeof(foo), "condor/%s", g->name);
		if((c = cgroup_new_cgroup(foo)) == NULL)	{
			fprintf(stderr, "CGroup error allocating %s\n",foo);
			exit(EXIT_FAILURE);
		}
		if((ret = cgroup_get_cgroup(c)) != 0)	{
			fprintf(stderr, "Error getting %s data: %s",
					g->name, cgroup_strerror(ret));
			exit(EXIT_FAILURE);
		}
		cont = cgroup_get_controller(c, "memory");
		get_memory_stats(cont, g);

		cgroup_free(&c);
	}
}

void print_groups(void)
{
	struct condor_group *g = groups;

	for(int i = 0; i < n_groups; i++, g = &groups[i])	{
		printf("Group %d: %s\n", i, g->name);
		printf("\tRSS: %lu\n\tSWAP: %lu\n", g->rss_used, g->swap_used);
		printf("\tProcesses (threads): %d (%d)\n", g->num_procs, g->num_tasks);
	}
}

int main(int argc, char *argv[])
{
	int ret;

	if((ret = cgroup_init()) != 0) {
		fprintf(stderr, "Error initalizing libcgroup: %s\n", cgroup_strerror(ret));
		return 1;
	}
	get_condor_cgroups();
	get_statistics();
	print_groups();
	free(groups);
	return 0;
}
