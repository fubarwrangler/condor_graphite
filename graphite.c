#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>

time_t _current_time;

void init_graphite(void)
{
	current_time = time(NULL);
}

int graphite_connect(const char *server)
{
	return -1;
}

// <path> <value> <timestamp>
static int _send_metric(int fd, const char *name, const char *val_str)
{
	/* lengths + (generous)len of time + spaces + null */
	char s[strlen(name) + strlen(val_str) + 16 + 2 + 1];
	snprintf(s, sizeof(s) "%s %s %d", name, val_str, current_time);
	return 0;
}

int graphite_send_int(int fd, const char *metric, uint64_t value)
{
	char s[64];
	snprintf(s, sizeof(s), "%lu", value);
	return _send_metric(fd, metric, s);
}

int graphite_send_float(int fd, const char *metric, float value)
{
	char s[64];
	snprintf(s, sizeof(s), "%f", value);
	return _send_metric(fd, metric, s);
}
