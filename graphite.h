#ifndef _GRAPHITE_H_
#define _GRAPHITE_H_
#include <stdint.h>

enum {
	GRAPHITE_TCP,
	GRAPHITE_UDP
};

void graphite_init(int connection_type);
int graphite_connect(const char *server, const char *port);
int graphite_send_uint(int fd, const char *metric, uint64_t value);
int graphite_send_int(int fd, const char *metric, int64_t value);
int graphite_send_float(int fd, const char *metric, float value);
void graphite_close(int fd);

extern int graphite_debug;

#endif
