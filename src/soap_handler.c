#include "soap_handler.h"
#include "player.h"

#include <stdio.h>
#include <string.h>


#define MAX_URI_LENGTH 2048

static TransportState s_state = TRANSPORT_NO_MEDIA;
static char s_current_uri[MAX_URI_LENGTH] = {0};
static char s_response_buf[4096];

// Command flags (set by SOAP handler, consumed by main loop)
static volatile int s_flag_new_uri = 0;
static volatile int s_flag_play = 0;
static volatile int s_flag_pause = 0;
static volatile int s_flag_stop = 0;

// ──────────── Helpers ────────────

// Simple XML tag value extractor
// Searches for <tag>value</tag> and copies value into buf
static int extract_xml_value(const char *xml, const char *tag, char *buf,
                             int bufsize) {
  if (!xml || !tag || !buf)
    return -1;

  // Search for opening tag - try multiple formats
  char open_tag[256];
  char *start = NULL;

  // Try <tag>
  snprintf(open_tag, sizeof(open_tag), "<%s>", tag);
  start = strstr(xml, open_tag);

  // Try <tag ...> (with attributes or namespace)
  if (!start) {
    snprintf(open_tag, sizeof(open_tag), "<%s ", tag);
    start = strstr(xml, open_tag);
    if (start) {
      // Find the > after attributes
      start = strchr(start, '>');
      if (start)
        start++;
    }
  }

  if (!start) {
    // Try namespace prefix variations (e.g. u:tag, ns:tag)
    const char *p = xml;
    while ((p = strchr(p, '<')) != NULL) {
      p++;
      // Skip closing tags
      if (*p == '/')
        continue;
      // Check if after potential prefix there's our tag
      const char *colon = strchr(p, ':');
      const char *gt = strchr(p, '>');
      const char *sp = strchr(p, ' ');

      if (colon && gt && colon < gt) {
        // There's a namespace prefix
        if (strncmp(colon + 1, tag, strlen(tag)) == 0) {
          char next_char = colon[1 + strlen(tag)];
          if (next_char == '>' || next_char == ' ') {
            start = strchr(p - 1, '>');
            if (start)
              start++;
            break;
          }
        }
      }
    }
  }

  if (!start) {
    start = strstr(xml, open_tag + 1);
    if (!start)
      return -1;
    start += strlen(open_tag) - 1;
  } else if (start == strstr(xml, open_tag)) {
    start += strlen(open_tag);
  }

  // Find closing tag </tag> or </prefix:tag>
  char close_tag[256];
  snprintf(close_tag, sizeof(close_tag), "</%s>", tag);
  char *end = strstr(start, close_tag);

  // Try with namespace prefix in closing tag
  if (!end) {
    const char *p = start;
    while ((p = strstr(p, "</")) != NULL) {
      p += 2;
      const char *colon = strchr(p, ':');
      const char *gt = strchr(p, '>');
      if (colon && gt && colon < gt) {
        if (strncmp(colon + 1, tag, strlen(tag)) == 0 &&
            colon[1 + strlen(tag)] == '>') {
          end = (char *)(p - 2);
          break;
        }
      }
    }
  }

  if (!end)
    return -1;

  int len = (int)(end - start);
  if (len >= bufsize)
    len = bufsize - 1;
  strncpy(buf, start, len);
  buf[len] = '\0';

  return len;
}

static const char *state_to_string(TransportState state) {
  switch (state) {
  case TRANSPORT_NO_MEDIA:
    return "NO_MEDIA_PRESENT";
  case TRANSPORT_STOPPED:
    return "STOPPED";
  case TRANSPORT_PLAYING:
    return "PLAYING";
  case TRANSPORT_PAUSED:
    return "PAUSED_PLAYBACK";
  case TRANSPORT_TRANSITIONING:
    return "TRANSITIONING";
  default:
    return "STOPPED";
  }
}

static void format_time(uint64_t ms, char *buf, int bufsize) {
  int hours = (int)(ms / 3600000);
  int minutes = (int)((ms % 3600000) / 60000);
  int seconds = (int)((ms % 60000) / 1000);
  snprintf(buf, bufsize, "%d:%02d:%02d", hours, minutes, seconds);
}

// ──────────── SOAP Response Builders ────────────

static const char *build_simple_response(const char *action_name) {
  snprintf(
      s_response_buf, sizeof(s_response_buf),
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
      "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
      "<s:Body>"
      "<u:%sResponse xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
      "</u:%sResponse>"
      "</s:Body></s:Envelope>",
      action_name, action_name);
  return s_response_buf;
}

static const char *build_transport_info_response(void) {
  snprintf(s_response_buf, sizeof(s_response_buf),
           "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
           "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
           "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
           "<s:Body>"
           "<u:GetTransportInfoResponse "
           "xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
           "<CurrentTransportState>%s</CurrentTransportState>"
           "<CurrentTransportStatus>OK</CurrentTransportStatus>"
           "<CurrentSpeed>1</CurrentSpeed>"
           "</u:GetTransportInfoResponse>"
           "</s:Body></s:Envelope>",
           state_to_string(s_state));
  return s_response_buf;
}

static const char *build_position_info_response(void) {
  char rel_time[32], track_dur[32];
  uint64_t pos = player_get_position_ms();
  uint64_t dur = player_get_duration_ms();
  format_time(pos, rel_time, sizeof(rel_time));
  format_time(dur, track_dur, sizeof(track_dur));

  snprintf(s_response_buf, sizeof(s_response_buf),
           "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
           "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
           "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
           "<s:Body>"
           "<u:GetPositionInfoResponse "
           "xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
           "<Track>1</Track>"
           "<TrackDuration>%s</TrackDuration>"
           "<TrackMetaData></TrackMetaData>"
           "<TrackURI>%s</TrackURI>"
           "<RelTime>%s</RelTime>"
           "<AbsTime>%s</AbsTime>"
           "<RelCount>0</RelCount>"
           "<AbsCount>0</AbsCount>"
           "</u:GetPositionInfoResponse>"
           "</s:Body></s:Envelope>",
           track_dur, s_current_uri, rel_time, rel_time);
  return s_response_buf;
}

// ──────────── Public API ────────────

TransportState soap_get_transport_state(void) { return s_state; }

const char *soap_get_current_uri(void) { return s_current_uri; }

void soap_set_transport_state(TransportState state) { s_state = state; }

int soap_has_new_uri(void) {
  if (s_flag_new_uri) {
    s_flag_new_uri = 0;
    return 1;
  }
  return 0;
}

int soap_has_play_command(void) {
  if (s_flag_play) {
    s_flag_play = 0;
    return 1;
  }
  return 0;
}

int soap_has_pause_command(void) {
  if (s_flag_pause) {
    s_flag_pause = 0;
    return 1;
  }
  return 0;
}

int soap_has_stop_command(void) {
  if (s_flag_stop) {
    s_flag_stop = 0;
    return 1;
  }
  return 0;
}

const char *soap_handle_request(const char *soap_action, const char *body) {
  if (!soap_action || !body)
    return NULL;

  // Determine action from SOAPAction header
  if (strstr(soap_action, "SetAVTransportURI")) {
    // Extract CurrentURI from body
    char uri[MAX_URI_LENGTH] = {0};
    if (extract_xml_value(body, "CurrentURI", uri, sizeof(uri)) > 0) {
      strncpy(s_current_uri, uri, sizeof(s_current_uri) - 1);
      s_current_uri[sizeof(s_current_uri) - 1] = '\0';
      s_state = TRANSPORT_STOPPED;
      s_flag_new_uri = 1;
    }
    return build_simple_response("SetAVTransportURI");
  } else if (strstr(soap_action, "#Play")) {
    s_state = TRANSPORT_TRANSITIONING;
    s_flag_play = 1;
    return build_simple_response("Play");
  } else if (strstr(soap_action, "#Pause")) {
    s_state = TRANSPORT_PAUSED;
    s_flag_pause = 1;
    return build_simple_response("Pause");
  } else if (strstr(soap_action, "#Stop")) {
    s_state = TRANSPORT_STOPPED;
    s_flag_stop = 1;
    return build_simple_response("Stop");
  } else if (strstr(soap_action, "GetTransportInfo")) {
    return build_transport_info_response();
  } else if (strstr(soap_action, "GetPositionInfo")) {
    return build_position_info_response();
  } else if (strstr(soap_action, "GetMediaInfo")) {
    // Minimal GetMediaInfo response
    snprintf(
        s_response_buf, sizeof(s_response_buf),
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:GetMediaInfoResponse "
        "xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
        "<NrTracks>1</NrTracks>"
        "<MediaDuration>0:00:00</MediaDuration>"
        "<CurrentURI>%s</CurrentURI>"
        "<CurrentURIMetaData></CurrentURIMetaData>"
        "<NextURI></NextURI>"
        "<NextURIMetaData></NextURIMetaData>"
        "<PlayMedium>NETWORK</PlayMedium>"
        "<RecordMedium>NOT_IMPLEMENTED</RecordMedium>"
        "<WriteStatus>NOT_IMPLEMENTED</WriteStatus>"
        "</u:GetMediaInfoResponse>"
        "</s:Body></s:Envelope>",
        s_current_uri);
    return s_response_buf;
  }

  // Unknown action - return empty successful response
  return build_simple_response("Unknown");
}

const char *soap_handle_connmgr_request(const char *soap_action,
                                        const char *body) {
  (void)body;

  if (strstr(soap_action, "GetProtocolInfo")) {
    snprintf(
        s_response_buf, sizeof(s_response_buf),
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:GetProtocolInfoResponse "
        "xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">"
        "<Source></Source>"
        "<Sink>http-get:*:video/mp4:*,http-get:*:video/x-matroska:*,"
        "http-get:*:video/avi:*,http-get:*:video/mpeg:*,"
        "http-get:*:application/x-mpegURL:*,http-get:*:application/"
        "vnd.apple.mpegurl:*,"
        "http-get:*:video/mp2t:*,http-get:*:audio/mpeg:*,"
        "http-get:*:audio/mp4:*,http-get:*:audio/ogg:*</Sink>"
        "</u:GetProtocolInfoResponse>"
        "</s:Body></s:Envelope>");
    return s_response_buf;
  } else if (strstr(soap_action, "GetCurrentConnectionIDs")) {
    snprintf(
        s_response_buf, sizeof(s_response_buf),
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:GetCurrentConnectionIDsResponse "
        "xmlns:u=\"urn:schemas-upnp-org:service:ConnectionManager:1\">"
        "<ConnectionIDs>0</ConnectionIDs>"
        "</u:GetCurrentConnectionIDsResponse>"
        "</s:Body></s:Envelope>");
    return s_response_buf;
  }

  return build_simple_response("Unknown");
}
