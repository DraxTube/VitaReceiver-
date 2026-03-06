#ifndef PLAYER_H
#define PLAYER_H

#include <psp2/types.h>

typedef enum {
  PLAYER_IDLE = 0,
  PLAYER_LOADING,
  PLAYER_PLAYING,
  PLAYER_PAUSED,
  PLAYER_STOPPED,
  PLAYER_ERROR
} PlayerState;

// Initialize the player module (sceAvPlayer, audio thread)
int player_init(void);

// Start playing a URL (HTTP/HTTPS direct or HLS)
int player_play(const char *url);

// Pause playback
int player_pause(void);

// Resume playback
int player_resume(void);

// Stop playback
int player_stop(void);

// Called every frame to get video data and render it
// Returns 1 if a frame was rendered, 0 otherwise
int player_render_frame(void);

// Check if player is still active (not finished/errored)
int player_is_active(void);

// Get current player state
PlayerState player_get_state(void);

// Get current playback position in milliseconds
uint64_t player_get_position_ms(void);

// Get total duration in milliseconds (0 if unknown)
uint64_t player_get_duration_ms(void);

// Cleanup
void player_term(void);

#endif // PLAYER_H
