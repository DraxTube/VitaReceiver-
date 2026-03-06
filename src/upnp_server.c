#include "upnp_server.h"
#include "network.h"
#include "soap_handler.h"


#include <stdio.h>
#include <string.h>


#include <psp2/net/net.h>

static int s_server_sock = -1;

// ──────────── XML Templates ────────────

static const char DEVICE_XML_TEMPLATE[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
    "<root xmlns=\"urn:schemas-upnp-org:device-1-0\">\r\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
    "  <device>\r\n"
    "    "
    "<deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType>\r\n"
    "    <friendlyName>VitaReceiver</friendlyName>\r\n"
    "    <manufacturer>Homebrew</manufacturer>\r\n"
    "    <manufacturerURL>https://github.com</manufacturerURL>\r\n"
    "    <modelDescription>DLNA Media Renderer for PS "
    "Vita</modelDescription>\r\n"
    "    <modelName>VitaReceiver</modelName>\r\n"
    "    <modelNumber>1.0</modelNumber>\r\n"
    "    <UDN>" UPNP_DEVICE_UDN "</UDN>\r\n"
    "    <serviceList>\r\n"
    "      <service>\r\n"
    "        "
    "<serviceType>urn:schemas-upnp-org:service:AVTransport:1</serviceType>\r\n"
    "        <serviceId>urn:upnp-org:serviceId:AVTransport</serviceId>\r\n"
    "        <controlURL>/AVTransport/control</controlURL>\r\n"
    "        <eventSubURL>/AVTransport/event</eventSubURL>\r\n"
    "        <SCPDURL>/AVTransport.xml</SCPDURL>\r\n"
    "      </service>\r\n"
    "      <service>\r\n"
    "        "
    "<serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</"
    "serviceType>\r\n"
    "        "
    "<serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId>\r\n"
    "        <controlURL>/ConnectionManager/control</controlURL>\r\n"
    "        <eventSubURL>/ConnectionManager/event</eventSubURL>\r\n"
    "        <SCPDURL>/ConnectionManager.xml</SCPDURL>\r\n"
    "      </service>\r\n"
    "      <service>\r\n"
    "        "
    "<serviceType>urn:schemas-upnp-org:service:RenderingControl:1</"
    "serviceType>\r\n"
    "        <serviceId>urn:upnp-org:serviceId:RenderingControl</serviceId>\r\n"
    "        <controlURL>/RenderingControl/control</controlURL>\r\n"
    "        <eventSubURL>/RenderingControl/event</eventSubURL>\r\n"
    "        <SCPDURL>/RenderingControl.xml</SCPDURL>\r\n"
    "      </service>\r\n"
    "    </serviceList>\r\n"
    "  </device>\r\n"
    "</root>\r\n";

static const char AVTRANSPORT_SCPD[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
    "  <actionList>\r\n"
    "    <action><name>SetAVTransportURI</name><argumentList>"
    "      "
    "<argument><name>InstanceID</name><direction>in</"
    "direction><relatedStateVariable>A_ARG_TYPE_InstanceID</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>CurrentURI</name><direction>in</"
    "direction><relatedStateVariable>AVTransportURI</relatedStateVariable></"
    "argument>"
    "      "
    "<argument><name>CurrentURIMetaData</name><direction>in</"
    "direction><relatedStateVariable>AVTransportURIMetaData</"
    "relatedStateVariable></argument>"
    "    </argumentList></action>\r\n"
    "    <action><name>Play</name><argumentList>"
    "      "
    "<argument><name>InstanceID</name><direction>in</"
    "direction><relatedStateVariable>A_ARG_TYPE_InstanceID</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>Speed</name><direction>in</"
    "direction><relatedStateVariable>TransportPlaySpeed</"
    "relatedStateVariable></argument>"
    "    </argumentList></action>\r\n"
    "    <action><name>Pause</name><argumentList>"
    "      "
    "<argument><name>InstanceID</name><direction>in</"
    "direction><relatedStateVariable>A_ARG_TYPE_InstanceID</"
    "relatedStateVariable></argument>"
    "    </argumentList></action>\r\n"
    "    <action><name>Stop</name><argumentList>"
    "      "
    "<argument><name>InstanceID</name><direction>in</"
    "direction><relatedStateVariable>A_ARG_TYPE_InstanceID</"
    "relatedStateVariable></argument>"
    "    </argumentList></action>\r\n"
    "    <action><name>GetTransportInfo</name><argumentList>"
    "      "
    "<argument><name>InstanceID</name><direction>in</"
    "direction><relatedStateVariable>A_ARG_TYPE_InstanceID</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>CurrentTransportState</name><direction>out</"
    "direction><relatedStateVariable>TransportState</relatedStateVariable></"
    "argument>"
    "      "
    "<argument><name>CurrentTransportStatus</name><direction>out</"
    "direction><relatedStateVariable>TransportStatus</relatedStateVariable></"
    "argument>"
    "      "
    "<argument><name>CurrentSpeed</name><direction>out</"
    "direction><relatedStateVariable>TransportPlaySpeed</"
    "relatedStateVariable></argument>"
    "    </argumentList></action>\r\n"
    "    <action><name>GetPositionInfo</name><argumentList>"
    "      "
    "<argument><name>InstanceID</name><direction>in</"
    "direction><relatedStateVariable>A_ARG_TYPE_InstanceID</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>Track</name><direction>out</"
    "direction><relatedStateVariable>CurrentTrack</relatedStateVariable></"
    "argument>"
    "      "
    "<argument><name>TrackDuration</name><direction>out</"
    "direction><relatedStateVariable>CurrentTrackDuration</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>TrackMetaData</name><direction>out</"
    "direction><relatedStateVariable>CurrentTrackMetaData</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>TrackURI</name><direction>out</"
    "direction><relatedStateVariable>CurrentTrackURI</relatedStateVariable></"
    "argument>"
    "      "
    "<argument><name>RelTime</name><direction>out</"
    "direction><relatedStateVariable>RelativeTimePosition</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>AbsTime</name><direction>out</"
    "direction><relatedStateVariable>AbsoluteTimePosition</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>RelCount</name><direction>out</"
    "direction><relatedStateVariable>RelativeCounterPosition</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>AbsCount</name><direction>out</"
    "direction><relatedStateVariable>AbsoluteCounterPosition</"
    "relatedStateVariable></argument>"
    "    </argumentList></action>\r\n"
    "  </actionList>\r\n"
    "  <serviceStateTable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>AVTransportURI</name><dataType>string</dataType></"
    "stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>AVTransportURIMetaData</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"yes\"><name>TransportState</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>TransportStatus</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>TransportPlaySpeed</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>CurrentTrack</name><dataType>ui4</dataType></"
    "stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>CurrentTrackDuration</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>CurrentTrackMetaData</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>CurrentTrackURI</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>RelativeTimePosition</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>AbsoluteTimePosition</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>RelativeCounterPosition</name><dataType>i4</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>AbsoluteCounterPosition</name><dataType>i4</"
    "dataType></stateVariable>\r\n"
    "  </serviceStateTable>\r\n"
    "</scpd>\r\n";

static const char CONNMGR_SCPD[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
    "  <actionList>\r\n"
    "    <action><name>GetProtocolInfo</name><argumentList>"
    "      "
    "<argument><name>Source</name><direction>out</"
    "direction><relatedStateVariable>SourceProtocolInfo</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>Sink</name><direction>out</"
    "direction><relatedStateVariable>SinkProtocolInfo</relatedStateVariable></"
    "argument>"
    "    </argumentList></action>\r\n"
    "    <action><name>GetCurrentConnectionIDs</name><argumentList>"
    "      "
    "<argument><name>ConnectionIDs</name><direction>out</"
    "direction><relatedStateVariable>CurrentConnectionIDs</"
    "relatedStateVariable></argument>"
    "    </argumentList></action>\r\n"
    "  </actionList>\r\n"
    "  <serviceStateTable>\r\n"
    "    <stateVariable "
    "sendEvents=\"yes\"><name>SourceProtocolInfo</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"yes\"><name>SinkProtocolInfo</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"yes\"><name>CurrentConnectionIDs</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "  </serviceStateTable>\r\n"
    "</scpd>\r\n";

static const char RENDERCONTROL_SCPD[] =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
    "<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\">\r\n"
    "  <specVersion><major>1</major><minor>0</minor></specVersion>\r\n"
    "  <actionList>\r\n"
    "    <action><name>GetVolume</name><argumentList>"
    "      "
    "<argument><name>InstanceID</name><direction>in</"
    "direction><relatedStateVariable>A_ARG_TYPE_InstanceID</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>Channel</name><direction>in</"
    "direction><relatedStateVariable>A_ARG_TYPE_Channel</"
    "relatedStateVariable></argument>"
    "      "
    "<argument><name>CurrentVolume</name><direction>out</"
    "direction><relatedStateVariable>Volume</relatedStateVariable></argument>"
    "    </argumentList></action>\r\n"
    "  </actionList>\r\n"
    "  <serviceStateTable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>A_ARG_TYPE_InstanceID</name><dataType>ui4</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"no\"><name>A_ARG_TYPE_Channel</name><dataType>string</"
    "dataType></stateVariable>\r\n"
    "    <stateVariable "
    "sendEvents=\"yes\"><name>Volume</name><dataType>ui2</dataType>"
    "      "
    "<allowedValueRange><minimum>0</minimum><maximum>100</maximum><step>1</"
    "step></allowedValueRange>"
    "    </stateVariable>\r\n"
    "  </serviceStateTable>\r\n"
    "</scpd>\r\n";

// ──────────── HTTP Server ────────────

static void send_http_response(int client_sock, int status,
                               const char *content_type, const char *body,
                               int body_len) {
  char header[512];
  const char *status_str = (status == 200) ? "200 OK" : "404 Not Found";

  int header_len = snprintf(header, sizeof(header),
                            "HTTP/1.1 %s\r\n"
                            "Content-Type: %s\r\n"
                            "Content-Length: %d\r\n"
                            "Connection: close\r\n"
                            "Server: VitaOS/1.0 UPnP/1.0 VitaReceiver/1.0\r\n"
                            "\r\n",
                            status_str, content_type, body_len);

  sceNetSend(client_sock, header, header_len, 0);
  if (body && body_len > 0) {
    sceNetSend(client_sock, body, body_len, 0);
  }
}

static void handle_client(int client_sock) {
  char request[8192];
  int total_received = 0;

  // Read the full HTTP request (headers + body)
  while (total_received < (int)sizeof(request) - 1) {
    int received = sceNetRecv(client_sock, request + total_received,
                              sizeof(request) - 1 - total_received, 0);
    if (received <= 0)
      break;
    total_received += received;

    // Check if we have full headers
    request[total_received] = '\0';
    char *header_end = strstr(request, "\r\n\r\n");
    if (header_end) {
      // Check Content-Length for POST requests
      char *cl = strstr(request, "Content-Length:");
      if (!cl)
        cl = strstr(request, "content-length:");
      if (cl) {
        int content_length = atoi(cl + 15);
        int header_size = (int)(header_end - request) + 4;
        int body_received = total_received - header_size;
        if (body_received >= content_length)
          break;
      } else {
        break; // No Content-Length, assume headers-only
      }
    }
  }
  request[total_received] = '\0';

  // Parse method and path
  char method[16] = {0};
  char path[256] = {0};
  sscanf(request, "%15s %255s", method, path);

  // Handle GET requests for UPnP descriptions
  if (strcmp(method, "GET") == 0) {
    if (strcmp(path, "/device.xml") == 0) {
      send_http_response(client_sock, 200, "text/xml; charset=\"utf-8\"",
                         DEVICE_XML_TEMPLATE, strlen(DEVICE_XML_TEMPLATE));
    } else if (strcmp(path, "/AVTransport.xml") == 0) {
      send_http_response(client_sock, 200, "text/xml; charset=\"utf-8\"",
                         AVTRANSPORT_SCPD, strlen(AVTRANSPORT_SCPD));
    } else if (strcmp(path, "/ConnectionManager.xml") == 0) {
      send_http_response(client_sock, 200, "text/xml; charset=\"utf-8\"",
                         CONNMGR_SCPD, strlen(CONNMGR_SCPD));
    } else if (strcmp(path, "/RenderingControl.xml") == 0) {
      send_http_response(client_sock, 200, "text/xml; charset=\"utf-8\"",
                         RENDERCONTROL_SCPD, strlen(RENDERCONTROL_SCPD));
    } else {
      const char *not_found = "Not Found";
      send_http_response(client_sock, 404, "text/plain", not_found,
                         strlen(not_found));
    }
  }
  // Handle POST requests for SOAP actions
  else if (strcmp(method, "POST") == 0) {
    // Extract SOAPAction header
    char soap_action[256] = {0};
    char *sa = strstr(request, "SOAPAction:");
    if (!sa)
      sa = strstr(request, "SOAPACTION:");
    if (!sa)
      sa = strstr(request, "soapaction:");
    if (sa) {
      sa += 11;
      while (*sa == ' ' || *sa == '"')
        sa++;
      int i = 0;
      while (sa[i] && sa[i] != '"' && sa[i] != '\r' && sa[i] != '\n' &&
             i < 255) {
        soap_action[i] = sa[i];
        i++;
      }
      soap_action[i] = '\0';
    }

    // Extract body
    char *body = strstr(request, "\r\n\r\n");
    if (body)
      body += 4;

    const char *response_body = NULL;

    if (strstr(path, "/AVTransport/control") != NULL) {
      response_body = soap_handle_request(soap_action, body);
    } else if (strstr(path, "/ConnectionManager/control") != NULL) {
      response_body = soap_handle_connmgr_request(soap_action, body);
    } else if (strstr(path, "/RenderingControl/control") != NULL) {
      // Minimal RenderingControl - return volume 100
      static const char vol_resp[] =
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
          "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
          "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
          "<s:Body>"
          "<u:GetVolumeResponse "
          "xmlns:u=\"urn:schemas-upnp-org:service:RenderingControl:1\">"
          "<CurrentVolume>100</CurrentVolume>"
          "</u:GetVolumeResponse>"
          "</s:Body></s:Envelope>";
      response_body = vol_resp;
    }

    if (response_body) {
      send_http_response(client_sock, 200, "text/xml; charset=\"utf-8\"",
                         response_body, strlen(response_body));
    } else {
      const char *err = "Bad Request";
      send_http_response(client_sock, 404, "text/plain", err, strlen(err));
    }
  }
  // Handle SUBSCRIBE for eventing (minimal - just accept)
  else if (strcmp(method, "SUBSCRIBE") == 0) {
    char sub_response[512];
    int len = snprintf(sub_response, sizeof(sub_response),
                       "HTTP/1.1 200 OK\r\n"
                       "SID: uuid:sub-vitareceiver-0001\r\n"
                       "TIMEOUT: Second-1800\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n"
                       "\r\n");
    sceNetSend(client_sock, sub_response, len, 0);
  }

  sceNetSocketClose(client_sock);
}

int upnp_server_init(void) {
  s_server_sock = sceNetSocket("upnp", SCE_NET_AF_INET, SCE_NET_SOCK_STREAM, 0);
  if (s_server_sock < 0)
    return s_server_sock;

  // Allow address reuse
  int reuse = 1;
  sceNetSetsockopt(s_server_sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR,
                   &reuse, sizeof(reuse));

  // Set non-blocking
  network_set_nonblocking(s_server_sock);

  // Bind
  SceNetSockaddrIn addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(UPNP_HTTP_PORT);
  addr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);

  int ret = sceNetBind(s_server_sock, (SceNetSockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    sceNetSocketClose(s_server_sock);
    s_server_sock = -1;
    return ret;
  }

  ret = sceNetListen(s_server_sock, 4);
  if (ret < 0) {
    sceNetSocketClose(s_server_sock);
    s_server_sock = -1;
    return ret;
  }

  return 0;
}

void upnp_server_poll(void) {
  if (s_server_sock < 0)
    return;

  SceNetSockaddrIn client_addr;
  unsigned int client_len = sizeof(client_addr);

  int client_sock =
      sceNetAccept(s_server_sock, (SceNetSockaddr *)&client_addr, &client_len);
  if (client_sock >= 0) {
    // Set a receive timeout on the client socket (2 seconds)
    int timeout = 2 * 1000 * 1000; // microseconds
    sceNetSetsockopt(client_sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_RCVTIMEO,
                     &timeout, sizeof(timeout));

    handle_client(client_sock);
  }
}

void upnp_server_term(void) {
  if (s_server_sock >= 0) {
    sceNetSocketClose(s_server_sock);
    s_server_sock = -1;
  }
}
