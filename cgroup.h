#ifndef _CCGROUP_H_
#define _CCGROUP_H_

#include <time.h>
#include <stdbool.h>

struct condor_group {
	char slot_name[12];	/*!< Extracted slot name */
	uint32_t sort_order;
	uint32_t num_procs;
	uint32_t num_tasks;
	uint64_t cpu_shares;
	uint64_t user_cpu_usage;
	uint64_t sys_cpu_usage;
	uint64_t rss_used;
	uint64_t swap_used;
	uint64_t cache_used;
	uint64_t mem_soft_limit;
	time_t start_time;
};

extern const char *default_cgroup_name;

#define for_each_group(g) for(struct condor_group *g = NULL; __group_for_each(&g);)

void read_condor_cgroup_info(const char *cg_name);

bool __group_for_each(struct condor_group **g);
bool groups_empty(void);
void cleanup_groups(void);

#endif
