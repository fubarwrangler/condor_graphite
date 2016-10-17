#ifndef _METRCS_H
#define _METRCS_H

#include "cgroup.h"
#include <stdbool.h>

int util_metric_send(int fd, const char *metric, bool buffer);
void buf_close(int fd);

/* Send metrics to a backend... a pointer to the the backend-specific sending-
 * function is passed in as @send_fn taking file-descriptor, metric, and value
 */
void send_group_metrics(struct condor_group *g, const char *hostname,
			const char *ns, int fd,
			int (*send_fn)(int, const char *, uint64_t));

#endif
