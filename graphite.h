#ifndef _GRAPHITE_H_
#define _GRAPHITE_H_
#include <stdint.h>

#include "util.h"

enum graphite_contype {
	GRAPHITE_TCP,
	GRAPHITE_UDP
};

/**
 * Initilize graphite library and choose the connection type (see header)
 *
 * @param[in] ctype  GRAPHITE_(TCP|UDP) for TCP/UDP connection
 */
void graphite_init(enum graphite_contype ctype);

/**
 * Connect to a graphite server and get a socket file-descripter back. Will
 * be either TCP/UDP based on how library was initalized.
 *
 * @param[in] server host to connect to (passed to getaddrinfo)
 * @param[in] port port number (string) or name (passed to getaddrinfo)
 *
 * @return socket file-descriptor
 */
int graphite_connect(const char *server, const char *port);

/**
 * Send unsigned-integer to graphite
 *
 * @param[in] fd graphite file-descriptor
 * @param[in] metric the metric-name to send (.-separated namespace)
 * @param[in] value the value to send
 *
 * @return 0 on success, -1 elsewise
 */
int graphite_send_uint(int fd, const char *metric, uint64_t value);

/**
 * Send signed integer to graphite
 * @see graphite_send_uint
 */
int graphite_send_int(int fd, const char *metric, int64_t value);

/**
 * Send floating-point number to graphite
 * @see graphite_send_uint
 */
int graphite_send_float(int fd, const char *metric, float value);

/**
 * Closes a graphite connection and de-initalizes the library
 *
 * @param[in] fd the socket-descripter returned by graphite connect
 */
void graphite_close(int fd);

#endif
