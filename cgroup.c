#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <linux/limits.h>
#include <libcgroup.h>

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
		printf("Found alread %s...\n", info->path);
		return 0;
	} else {
		printf("Adding group (%d) %s\n", n_groups + 1, info->path);
		if(NULL == (groups = realloc(groups,
				(1 + n_groups) * sizeof(struct condor_group))))
			exit(ENOMEM);
		*grp = &groups[n_groups++];
		root_len = strlen(info->full_path) - strlen(info->path);
		/* Really!? This is the way to access a structure-member's size? */
		strncpy((*grp)->name, info->path,
				sizeof(((struct condor_group *)0)->name));
		strncpy((*grp)->root_path, info->full_path, root_len);
		*((*grp)->root_path + root_len + 1) = '\0';
		return 0;
	}
}

/* Walk through the children of the "condor" cgroup under @controller
 *
 * return: 1 when there is more to go, 0 when done
 *
 * WARNING: This function uses static storage, not threadsafe!
 */
int walk_condor_cgroups(const char *controller, struct cgroup_file_info *info)
{
	static void *handle = NULL;
	static int level = -1;
	struct condor_group *g;
	int ret;

	if(handle == NULL)	{
		ret = cgroup_walk_tree_begin(controller, CONDOR_ROOT_GROUP, 1,
					     &handle, info, &level);
		if (ret != 0) {
			fprintf(stderr,
				"Error walking controller: %s", controller);
			exit(EXIT_FAILURE);
		}
		if(info->type == CGROUP_FILE_TYPE_DIR && info->depth == 1)
			add_group(info, &g);
	}
	while ((ret = cgroup_walk_tree_next(0, &handle, info, level)) != ECGEOF)	{
		if (ret != 0) {
			fprintf(stderr,
				"Error walking controller: %s", controller);
			exit(EXIT_FAILURE);
		}
		if(info->type == CGROUP_FILE_TYPE_DIR && info->depth == 1)
			add_group(info, &g);
	}

	cgroup_walk_tree_end(&handle);
	handle = NULL;
	return 0;
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


	// This doesn't work: need to read from file itself
	//cgroup_get_value_string(c, "cgroup.procs", &s);

}

void get_statistics()
{
	char foo[512];
	struct cgroup *c;
	struct condor_group *g = NULL;
	struct cgroup_controller *cont;
	int ret;

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

void get_condor_cgroups(void)
{
	struct cgroup_file_info info;
	struct condor_group *g = NULL;

	/* Walk the "memory" controller to get all cgroup names for now... */
	while(walk_condor_cgroups("memory", &info) != 0)	{
		add_group(&info, &g);
		printf("%d: %s <-- %s (%d)\n", info.type, g->name, info.parent, info.depth);
	}
}


void print_groups(void)
{
	struct condor_group *g = groups;

	for(int i = 0; i < n_groups; i++, g = &groups[i])	{
		printf("Group %d: %s\n", i, g->root_path);
		printf("\tRSS: %lu\n\tSWAP: %lu\n", g->rss_used, g->swap_used);
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
