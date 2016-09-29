#ifndef _CCGROUP_H_
#define _CCGROUP_H_

#include <time.h>

struct condor_group {
	char name[192];		/*!< Name of cgroup under root */
	char root_path[56];	/*!< Path where cgroup is mounted */
	char slot_name[16];	/*!< Extracted slot name */
	uint32_t sort_order;
	uint32_t num_procs;
	uint32_t num_tasks;
	uint64_t cpu_shares;
	uint64_t user_cpu_usage;
	uint64_t sys_cpu_usage;
	uint64_t rss_used;
	uint64_t swap_used;
	time_t start_time;
};

extern struct condor_group *groups;
extern int n_groups;

void get_condor_cgroups(const char *controller, const char *condor_cgroup);
void get_cgroup_statistics();

#endif
