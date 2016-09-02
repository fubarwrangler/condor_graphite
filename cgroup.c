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
	uint32_t num_procs;
	uint32_t cpu_shares;
	uint64_t user_cpu_usage;
	uint64_t sys_cpu_usage;
	uint64_t memory_usage;
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
	if(NULL != (*grp = find_group(info->path)))	{
		printf("Found alread %s...\n", info->path);
		return 0;
	} else {
		printf("Adding group (%d) %s\n", n_groups + 1, info->path);
		if(NULL == (groups = realloc(groups,
				(1 + n_groups) * sizeof(struct condor_group))))
			exit(ENOMEM);
		*grp = &groups[n_groups++];
		/* Really!? This is the way to access a structure-member's size? */
		strncpy((*grp)->name, info->path, sizeof(((struct condor_group *)0)->name));
		return 0;
	}
}

void print_groups(void)
{
	for(int i = 0; i < n_groups; i++)	{
		printf("Group %d: %s\n", i, groups[i].name);
	}
}


/* Walk through the children of the "condor" cgroup under @controller
 *
 * return: 1 when there is more to go, 0 when done
 *
 * WARNING: This function uses static storage, not threadsafe!
 */
int walk_condor_cgroups(char *controller, struct cgroup_file_info *info)
{
	static void *handle = NULL;
	static int level = -1;
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
			return 1;
	}
	while ((ret = cgroup_walk_tree_next(0, &handle, info, level)) != ECGEOF)	{
		if (ret != 0) {
			fprintf(stderr,
				"Error walking controller: %s", controller);
			exit(EXIT_FAILURE);
		}
		if(info->type == CGROUP_FILE_TYPE_DIR && info->depth == 1)
			return 1;
	}

	cgroup_walk_tree_end(&handle);
	handle = NULL;
	return 0;
}


void gather_memory_stats(void)
{
	struct cgroup_file_info info;
	struct condor_group *g = NULL;

	while(walk_condor_cgroups("cpuacct", &info) != 0)	{
		add_group(&info, &g);
		printf("%d: %s <-- %s (%d)\n", info.type, g->name, info.parent, info.depth);
	}
	printf("\n\n******************************************************\n\n\n");
	while(walk_condor_cgroups("memory", &info) != 0)	{
		add_group(&info, &g);
		printf("%d: %s <-- %s (%d)\n", info.type, g->name, info.parent, info.depth);
	}
}

int main(int argc, char *argv[])
{
	int ret;

	if((ret = cgroup_init()) != 0) {
		fprintf(stderr, "Error initalizing libcgroup: %s\n", cgroup_strerror(ret));
		return 1;
	}
	gather_memory_stats();
	print_groups();
	free(groups);
	return 0;
}
