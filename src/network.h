#ifndef NETWORK_H
#define NETWORK_H

#include <psp2/types.h>

// Initialize networking (sceNet, sceNetCtl, sceHttp, sceSsl)
int network_init(void);

// Get the local IP address as a string (e.g. "192.168.1.100")
// Returns pointer to static buffer
const char *network_get_ip(void);

// Get the local IP as uint32_t (network byte order)
uint32_t network_get_ip_uint(void);

// Set a socket to non-blocking mode
int network_set_nonblocking(int sock);

// Cleanup networking
void network_term(void);

#endif // NETWORK_H
