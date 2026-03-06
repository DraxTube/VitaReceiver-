#ifndef UPNP_SERVER_H
#define UPNP_SERVER_H

#define UPNP_HTTP_PORT 49152

// UDN for this device (fixed UUID)
#define UPNP_DEVICE_UDN "uuid:vitareceiver-0001-0001-0001-000000000001"

// Initialize the UPnP HTTP server (non-blocking TCP)
int upnp_server_init(void);

// Poll for incoming HTTP connections and handle requests (non-blocking)
void upnp_server_poll(void);

// Cleanup
void upnp_server_term(void);

#endif // UPNP_SERVER_H
