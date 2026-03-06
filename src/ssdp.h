#ifndef SSDP_H
#define SSDP_H

#define SSDP_MULTICAST_ADDR  "239.255.255.250"
#define SSDP_PORT            1900
#define SSDP_NOTIFY_INTERVAL 30  // seconds between NOTIFY alive messages

// Initialize SSDP: create multicast socket, join group
int ssdp_init(void);

// Poll for M-SEARCH requests and respond (non-blocking)
// Also sends periodic NOTIFY alive messages
void ssdp_poll(void);

// Send ssdp:byebye notification
void ssdp_send_byebye(void);

// Cleanup
void ssdp_term(void);

#endif // SSDP_H
