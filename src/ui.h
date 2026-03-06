#ifndef UI_H
#define UI_H

// Initialize UI (vita2d, load font)
int ui_init(void);

// Begin frame rendering
void ui_begin_frame(void);

// End frame rendering and swap
void ui_end_frame(void);

// Draw the idle screen (waiting for connection)
void ui_draw_idle(const char *ip_address);

// Draw the loading screen
void ui_draw_loading(const char *url);

// Draw the playing overlay (shown briefly or on button press)
void ui_draw_playing_overlay(const char *url, uint64_t position_ms,
                             uint64_t duration_ms);

// Draw an error message
void ui_draw_error(const char *message);

// Cleanup
void ui_term(void);

#endif // UI_H
