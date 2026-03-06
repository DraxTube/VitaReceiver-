#include "hls.h"
#include "network.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/net/http.h>

// Segment storage
static char s_segments[HLS_MAX_SEGMENTS][1024];
static int s_segment_count = 0;
static int s_segment_index = 0;
static char s_base_url[1024];
static char s_temp_path[256];

// Extract base URL from a full URL (everything up to and including last /)
static void extract_base_url(const char *url, char *base, int base_size) {
  strncpy(base, url, base_size - 1);
  base[base_size - 1] = '\0';

  char *last_slash = strrchr(base, '/');
  if (last_slash) {
    *(last_slash + 1) = '\0';
  }
}

// Download content from a URL into a buffer
// Returns bytes downloaded, or -1 on error
static int download_to_buffer(const char *url, char *buf, int bufsize) {
  int tpl = sceHttpCreateTemplate("VitaReceiver/1.0", 2, 1);
  if (tpl < 0)
    return -1;

  int conn = sceHttpCreateConnectionWithURL(tpl, url, 0);
  if (conn < 0) {
    sceHttpDeleteTemplate(tpl);
    return -1;
  }

  int req = sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, url, 0);
  if (req < 0) {
    sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tpl);
    return -1;
  }

  // Allow redirects
  sceHttpSetAutoRedirect(req, 1);

  int ret = sceHttpSendRequest(req, NULL, 0);
  if (ret < 0) {
    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tpl);
    return -1;
  }

  int status_code;
  sceHttpGetStatusCode(req, &status_code);
  if (status_code != 200) {
    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tpl);
    return -1;
  }

  int total = 0;
  while (total < bufsize - 1) {
    int read = sceHttpReadData(req, buf + total, bufsize - 1 - total);
    if (read <= 0)
      break;
    total += read;
  }
  buf[total] = '\0';

  sceHttpDeleteRequest(req);
  sceHttpDeleteConnection(conn);
  sceHttpDeleteTemplate(tpl);

  return total;
}

int hls_is_playlist(const char *url) {
  if (!url)
    return 0;
  const char *ext = strrchr(url, '.');
  if (!ext)
    return 0;

  // Check for .m3u8 or .m3u
  if (strncasecmp(ext, ".m3u8", 5) == 0)
    return 1;
  if (strncasecmp(ext, ".m3u", 4) == 0)
    return 1;

  // Also check for m3u8 in query string (some URLs have ?format=m3u8)
  if (strstr(url, "m3u8") != NULL)
    return 1;

  return 0;
}

int hls_init(void) {
  s_segment_count = 0;
  s_segment_index = 0;

  // Create temp directory
  sceIoMkdir(HLS_TEMP_DIR, 0777);
  sceIoMkdir("ux0:data/VitaReceiver", 0777);

  return 0;
}

// Parse a media playlist to extract segment URLs
static int parse_media_playlist(const char *data, const char *base_url) {
  s_segment_count = 0;

  const char *line = data;
  while (line && *line && s_segment_count < HLS_MAX_SEGMENTS) {
    // Skip to next line
    const char *eol = strchr(line, '\n');
    int line_len = eol ? (int)(eol - line) : (int)strlen(line);

    // Remove trailing \r
    int actual_len = line_len;
    if (actual_len > 0 && line[actual_len - 1] == '\r')
      actual_len--;

    // Skip empty lines and comments/tags
    if (actual_len > 0 && line[0] != '#') {
      // This is a segment URL
      char segment_url[1024];
      if (line[0] == '/' || strncmp(line, "http", 4) == 0) {
        // Absolute URL
        snprintf(segment_url, sizeof(segment_url), "%.*s", actual_len, line);
      } else {
        // Relative URL
        snprintf(segment_url, sizeof(segment_url), "%s%.*s", base_url,
                 actual_len, line);
      }

      strncpy(s_segments[s_segment_count], segment_url,
              sizeof(s_segments[0]) - 1);
      s_segments[s_segment_count][sizeof(s_segments[0]) - 1] = '\0';
      s_segment_count++;
    }

    if (!eol)
      break;
    line = eol + 1;
  }

  return s_segment_count;
}

// Parse a master playlist to find the best variant
static int parse_master_playlist(const char *data, const char *url) {
  // Find the highest bandwidth variant that's <= 720p
  const char *best_url = NULL;
  int best_bandwidth = 0;
  int best_url_len = 0;

  const char *line = data;
  while (line && *line) {
    const char *eol = strchr(line, '\n');
    int line_len = eol ? (int)(eol - line) : (int)strlen(line);

    if (strncmp(line, "#EXT-X-STREAM-INF:", 18) == 0) {
      // Extract bandwidth
      const char *bw = strstr(line, "BANDWIDTH=");
      int bandwidth = 0;
      if (bw) {
        bandwidth = atoi(bw + 10);
      }

      // Check resolution (skip > 720p)
      const char *res = strstr(line, "RESOLUTION=");
      int ok = 1;
      if (res) {
        int w = 0, h = 0;
        sscanf(res + 11, "%dx%d", &w, &h);
        if (h > 720)
          ok = 0;
      }

      // Get the URL on the next line
      if (eol && ok) {
        const char *next_line = eol + 1;
        const char *next_eol = strchr(next_line, '\n');
        int next_len =
            next_eol ? (int)(next_eol - next_line) : (int)strlen(next_line);
        if (next_len > 0 && next_line[next_len - 1] == '\r')
          next_len--;

        if (next_len > 0 && next_line[0] != '#') {
          if (bandwidth > best_bandwidth) {
            best_bandwidth = bandwidth;
            best_url = next_line;
            best_url_len = next_len;
          }
        }
      }
    }

    if (!eol)
      break;
    line = eol + 1;
  }

  if (best_url) {
    // Build full URL for the variant playlist
    char variant_url[1024];
    if (best_url[0] == '/' || strncmp(best_url, "http", 4) == 0) {
      snprintf(variant_url, sizeof(variant_url), "%.*s", best_url_len,
               best_url);
    } else {
      char base[1024];
      extract_base_url(url, base, sizeof(base));
      snprintf(variant_url, sizeof(variant_url), "%s%.*s", base, best_url_len,
               best_url);
    }

    // Download and parse the variant playlist
    char *variant_data = malloc(256 * 1024);
    if (!variant_data)
      return -1;

    int ret = download_to_buffer(variant_url, variant_data, 256 * 1024);
    if (ret > 0) {
      char variant_base[1024];
      extract_base_url(variant_url, variant_base, sizeof(variant_base));
      ret = parse_media_playlist(variant_data, variant_base);
    }

    free(variant_data);
    return ret;
  }

  return -1;
}

int hls_load_playlist(const char *url) {
  s_segment_count = 0;
  s_segment_index = 0;

  extract_base_url(url, s_base_url, sizeof(s_base_url));

  // Download playlist
  char *playlist_data = malloc(256 * 1024);
  if (!playlist_data)
    return -1;

  int ret = download_to_buffer(url, playlist_data, 256 * 1024);
  if (ret <= 0) {
    free(playlist_data);
    return -1;
  }

  // Check if master playlist (contains #EXT-X-STREAM-INF)
  if (strstr(playlist_data, "#EXT-X-STREAM-INF") != NULL) {
    ret = parse_master_playlist(playlist_data, url);
  } else {
    ret = parse_media_playlist(playlist_data, s_base_url);
  }

  free(playlist_data);
  return ret;
}

const char *hls_get_next_segment(void) {
  if (s_segment_index >= s_segment_count)
    return NULL;
  return s_segments[s_segment_index++];
}

const char *hls_download_segment(const char *segment_url) {
  if (!segment_url)
    return NULL;

  snprintf(s_temp_path, sizeof(s_temp_path), "%s/segment_%d.ts", HLS_TEMP_DIR,
           s_segment_index - 1);

  int tpl = sceHttpCreateTemplate("VitaReceiver/1.0", 2, 1);
  if (tpl < 0)
    return NULL;

  int conn = sceHttpCreateConnectionWithURL(tpl, segment_url, 0);
  if (conn < 0) {
    sceHttpDeleteTemplate(tpl);
    return NULL;
  }

  int req =
      sceHttpCreateRequestWithURL(conn, SCE_HTTP_METHOD_GET, segment_url, 0);
  if (req < 0) {
    sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tpl);
    return NULL;
  }

  sceHttpSetAutoRedirect(req, 1);

  int ret = sceHttpSendRequest(req, NULL, 0);
  if (ret < 0) {
    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tpl);
    return NULL;
  }

  // Open output file
  SceUID fd =
      sceIoOpen(s_temp_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
  if (fd < 0) {
    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(conn);
    sceHttpDeleteTemplate(tpl);
    return NULL;
  }

  // Download and write
  char buf[32 * 1024];
  while (1) {
    int read = sceHttpReadData(req, buf, sizeof(buf));
    if (read <= 0)
      break;
    sceIoWrite(fd, buf, read);
  }

  sceIoClose(fd);
  sceHttpDeleteRequest(req);
  sceHttpDeleteConnection(conn);
  sceHttpDeleteTemplate(tpl);

  return s_temp_path;
}

void hls_reset(void) { s_segment_index = 0; }

void hls_term(void) {
  // Clean up temp files
  for (int i = 0; i < s_segment_count; i++) {
    char path[256];
    snprintf(path, sizeof(path), "%s/segment_%d.ts", HLS_TEMP_DIR, i);
    sceIoRemove(path);
  }

  s_segment_count = 0;
  s_segment_index = 0;
}
