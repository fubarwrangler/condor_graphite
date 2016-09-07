#ifndef _GRAPHITE_H_
#define _GRAPHITE_H_
#include <stdint.h>

void graphite_init(void);
int graphite_connect(const char *server, const char *port);
int graphite_send_uint(int fd, const char *metric, uint64_t value);
int graphite_send_int(int fd, const char *metric, int64_t value);
int graphite_send_float(int fd, const char *metric, float value);

#endif
