#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <vita2d.h>

static vita2d_pgf *s_font = NULL;

#define COLOR_WHITE RGBA8(255, 255, 255, 255)
#define COLOR_GRAY RGBA8(180, 180, 180, 255)
#define COLOR_GREEN RGBA8(100, 255, 100, 255)
#define COLOR_YELLOW RGBA8(255, 255, 100, 255)
#define COLOR_RED RGBA8(255, 100, 100, 255)
#define COLOR_BG RGBA8(20, 20, 30, 255)
#define COLOR_CYAN RGBA8(100, 200, 255, 255)

int ui_init(void) {
  vita2d_init();
  vita2d_set_clear_color(COLOR_BG);

  s_font = vita2d_load_default_pgf();
  if (!s_font)
    return -1;

  return 0;
}

void ui_begin_frame(void) {
  vita2d_start_drawing();
  vita2d_clear_screen();
}

void ui_end_frame(void) {
  vita2d_end_drawing();
  vita2d_swap_buffers();
  vita2d_wait_rendering_done();
}

void ui_draw_idle(const char *ip_address) {
  // Title
  vita2d_pgf_draw_text(s_font, 280, 100, COLOR_CYAN, 1.5f, "VitaReceiver");

  // Subtitle
  vita2d_pgf_draw_text(s_font, 240, 160, COLOR_GRAY, 1.0f,
                       "DLNA Media Renderer for PS Vita");

  // Status
  vita2d_pgf_draw_text(s_font, 280, 250, COLOR_GREEN, 1.0f,
                       "Waiting for connection...");

  // IP info
  char ip_text[128];
  snprintf(ip_text, sizeof(ip_text), "IP Address: %s", ip_address);
  vita2d_pgf_draw_text(s_font, 300, 300, COLOR_WHITE, 1.0f, ip_text);

  // Instructions
  vita2d_pgf_draw_text(s_font, 160, 400, COLOR_GRAY, 0.8f,
                       "Open Web Video Caster and select 'VitaReceiver'");
  vita2d_pgf_draw_text(s_font, 280, 430, COLOR_GRAY, 0.8f,
                       "as your casting device");

  // Footer
  vita2d_pgf_draw_text(s_font, 350, 510, COLOR_GRAY, 0.7f, "START: Exit");
}

void ui_draw_loading(const char *url) {
  vita2d_pgf_draw_text(s_font, 350, 250, COLOR_YELLOW, 1.2f, "Loading...");

  // Show truncated URL
  char url_text[128];
  if (url && strlen(url) > 60) {
    snprintf(url_text, sizeof(url_text), "%.57s...", url);
  } else if (url) {
    snprintf(url_text, sizeof(url_text), "%s", url);
  } else {
    snprintf(url_text, sizeof(url_text), "(unknown)");
  }
  vita2d_pgf_draw_text(s_font, 80, 300, COLOR_GRAY, 0.7f, url_text);
}

void ui_draw_playing_overlay(const char *url, uint64_t position_ms,
                             uint64_t duration_ms) {
  (void)url;

  // Semi-transparent bar at bottom
  vita2d_draw_rectangle(0, 490, 960, 54, RGBA8(0, 0, 0, 160));

  // Time
  char time_text[64];
  int pos_min = (int)(position_ms / 60000);
  int pos_sec = (int)((position_ms % 60000) / 1000);

  if (duration_ms > 0) {
    int dur_min = (int)(duration_ms / 60000);
    int dur_sec = (int)((duration_ms % 60000) / 1000);
    snprintf(time_text, sizeof(time_text), "%02d:%02d / %02d:%02d", pos_min,
             pos_sec, dur_min, dur_sec);

    // Progress bar
    float progress = (float)position_ms / (float)duration_ms;
    if (progress > 1.0f)
      progress = 1.0f;
    vita2d_draw_rectangle(20, 496, 920, 4, RGBA8(60, 60, 60, 200));
    vita2d_draw_rectangle(20, 496, (int)(920 * progress), 4, COLOR_CYAN);
  } else {
    snprintf(time_text, sizeof(time_text), "%02d:%02d", pos_min, pos_sec);
  }

  vita2d_pgf_draw_text(s_font, 400, 530, COLOR_WHITE, 0.8f, time_text);
}

void ui_draw_error(const char *message) {
  vita2d_pgf_draw_text(s_font, 330, 250, COLOR_RED, 1.2f, "Error");
  vita2d_pgf_draw_text(s_font, 100, 300, COLOR_WHITE, 0.8f,
                       message ? message : "Unknown error");
  vita2d_pgf_draw_text(s_font, 300, 400, COLOR_GRAY, 0.8f, "CIRCLE: Dismiss");
}

void ui_term(void) {
  if (s_font) {
    vita2d_free_pgf(s_font);
    s_font = NULL;
  }
  vita2d_fini();
}
