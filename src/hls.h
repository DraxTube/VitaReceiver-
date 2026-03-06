#ifndef HLS_H
#define HLS_H

#define HLS_MAX_SEGMENTS 512
#define HLS_TEMP_DIR "ux0:data/VitaReceiver/temp"

// Check if a URL is an HLS playlist
int hls_is_playlist(const char *url);

// Initialize HLS module
int hls_init(void);

// Load and parse an HLS playlist from URL
// Returns number of segments found, or -1 on error
int hls_load_playlist(const char *url);

// Get the URL of the next segment to play
// Returns NULL if no more segments
const char *hls_get_next_segment(void);

// Download a segment to temp storage
// Returns local file path, or NULL on error
const char *hls_download_segment(const char *segment_url);

// Reset segment index (restart from beginning)
void hls_reset(void);

// Cleanup temp files and state
void hls_term(void);

#endif // HLS_H
