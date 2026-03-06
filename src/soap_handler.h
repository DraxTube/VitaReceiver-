#ifndef SOAP_HANDLER_H
#define SOAP_HANDLER_H

// Transport states
typedef enum {
  TRANSPORT_NO_MEDIA = 0,
  TRANSPORT_STOPPED,
  TRANSPORT_PLAYING,
  TRANSPORT_PAUSED,
  TRANSPORT_TRANSITIONING
} TransportState;

// Get current transport state
TransportState soap_get_transport_state(void);

// Get current media URI
const char *soap_get_current_uri(void);

// Handle a SOAP request body, returns response XML (static buffer)
// soap_action: the SOAPAction header value
// body: the full HTTP body (SOAP envelope XML)
const char *soap_handle_request(const char *soap_action, const char *body);

// Handle ConnectionManager SOAP requests
const char *soap_handle_connmgr_request(const char *soap_action,
                                        const char *body);

// Check if a new URI has been set (resets the flag after reading)
int soap_has_new_uri(void);

// Check if play command was received
int soap_has_play_command(void);

// Check if pause command was received
int soap_has_pause_command(void);

// Check if stop command was received
int soap_has_stop_command(void);

// Set transport state (called by player module)
void soap_set_transport_state(TransportState state);

#endif // SOAP_HANDLER_H
