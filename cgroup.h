#ifndef _CCGROUP_H_
#define _CCGROUP_H_

struct condor_group {
	char name[192];
	char root_path[256];
	char slot_name[16];
	uint32_t num_procs;
	uint32_t num_tasks;
	uint64_t cpu_shares;
	uint64_t user_cpu_usage;
	uint64_t sys_cpu_usage;
	uint64_t rss_used;
	uint64_t swap_used;
};

extern struct condor_group *groups;
extern int n_groups;

void get_condor_cgroups(const char *controller);
void get_cgroup_statistics();

#endif
