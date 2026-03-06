#include "hls.h"
#include "network.h"
#include "player.h"
#include "soap_handler.h"
#include "ssdp.h"
#include "ui.h"
#include "upnp_server.h"


#include <stdio.h>
#include <string.h>

#include <psp2/ctrl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/power.h>


// ──────────── App State ────────────

typedef enum {
  APP_IDLE = 0, // Waiting for DLNA connection
  APP_LOADING,  // Loading a video
  APP_PLAYING,  // Video playing
  APP_PAUSED,   // Video paused
  APP_ERROR     // Error state
} AppState;

static AppState s_app_state = APP_IDLE;
static char s_error_msg[256] = {0};
static int s_show_overlay = 0;
static int s_overlay_timer = 0;

// ──────────── Main ────────────

int main(void) {
  // Set CPU/GPU clocks to max for best performance
  scePowerSetArmClockFrequency(444);
  scePowerSetBusClockFrequency(222);
  scePowerSetGpuClockFrequency(222);
  scePowerSetGpuXbarClockFrequency(166);

  // Create data directory
  sceIoMkdir("ux0:data/VitaReceiver", 0777);
  sceIoMkdir("ux0:data/VitaReceiver/temp", 0777);

  // Initialize controller
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);

  // Initialize UI
  if (ui_init() < 0) {
    sceKernelExitProcess(0);
    return 1;
  }

  // Initialize networking
  if (network_init() < 0) {
    // Show error on screen for a moment
    for (int i = 0; i < 180; i++) {
      ui_begin_frame();
      ui_draw_error(
          "Failed to initialize network.\nMake sure WiFi is connected.");
      ui_end_frame();
    }
    ui_term();
    sceKernelExitProcess(0);
    return 1;
  }

  // Initialize player
  if (player_init() < 0) {
    network_term();
    ui_term();
    sceKernelExitProcess(0);
    return 1;
  }

  // Initialize SSDP
  if (ssdp_init() < 0) {
    player_term();
    network_term();
    ui_term();
    sceKernelExitProcess(0);
    return 1;
  }

  // Initialize UPnP server
  if (upnp_server_init() < 0) {
    ssdp_term();
    player_term();
    network_term();
    ui_term();
    sceKernelExitProcess(0);
    return 1;
  }

  // ──── Main Loop ────
  int running = 1;
  s_app_state = APP_IDLE;

  while (running) {
    // Read controller input
    SceCtrlData ctrl;
    sceCtrlPeekBufferPositive(0, &ctrl, 1);

    // START button: exit app (hold for 1 second via simple counter)
    static int start_hold = 0;
    if (ctrl.buttons & SCE_CTRL_START) {
      start_hold++;
      if (start_hold > 60) { // ~1 second at 60fps
        running = 0;
        continue;
      }
    } else {
      start_hold = 0;
    }

    // CIRCLE button: stop playback / dismiss error
    if (ctrl.buttons & SCE_CTRL_CIRCLE) {
      if (s_app_state == APP_PLAYING || s_app_state == APP_PAUSED) {
        player_stop();
        soap_set_transport_state(TRANSPORT_STOPPED);
        s_app_state = APP_IDLE;
      } else if (s_app_state == APP_ERROR) {
        s_app_state = APP_IDLE;
      }
    }

    // CROSS button: toggle overlay
    if (ctrl.buttons & SCE_CTRL_CROSS) {
      s_show_overlay = 1;
      s_overlay_timer = 180; // Show for 3 seconds
    }

    // TRIANGLE button: pause/resume
    if (ctrl.buttons & SCE_CTRL_TRIANGLE) {
      if (s_app_state == APP_PLAYING) {
        player_pause();
        soap_set_transport_state(TRANSPORT_PAUSED);
        s_app_state = APP_PAUSED;
      } else if (s_app_state == APP_PAUSED) {
        player_resume();
        soap_set_transport_state(TRANSPORT_PLAYING);
        s_app_state = APP_PLAYING;
      }
    }

    // ──── Poll network services ────
    ssdp_poll();
    upnp_server_poll();

    // ──── Process SOAP commands ────
    if (soap_has_new_uri()) {
      const char *uri = soap_get_current_uri();
      if (uri && uri[0]) {
        s_app_state = APP_LOADING;
      }
    }

    if (soap_has_play_command()) {
      const char *uri = soap_get_current_uri();
      if (uri && uri[0]) {
        if (s_app_state == APP_PAUSED) {
          player_resume();
          soap_set_transport_state(TRANSPORT_PLAYING);
          s_app_state = APP_PLAYING;
        } else {
          // Start playing the URI
          if (player_play(uri) == 0) {
            soap_set_transport_state(TRANSPORT_PLAYING);
            s_app_state = APP_PLAYING;
          } else {
            snprintf(s_error_msg, sizeof(s_error_msg), "Failed to play: %.200s",
                     uri);
            soap_set_transport_state(TRANSPORT_STOPPED);
            s_app_state = APP_ERROR;
          }
        }
      }
    }

    if (soap_has_pause_command()) {
      if (s_app_state == APP_PLAYING) {
        player_pause();
        soap_set_transport_state(TRANSPORT_PAUSED);
        s_app_state = APP_PAUSED;
      }
    }

    if (soap_has_stop_command()) {
      player_stop();
      soap_set_transport_state(TRANSPORT_STOPPED);
      s_app_state = APP_IDLE;
    }

    // ──── Check player state ────
    if (s_app_state == APP_PLAYING) {
      PlayerState ps = player_get_state();
      if (ps == PLAYER_STOPPED || ps == PLAYER_ERROR) {
        soap_set_transport_state(TRANSPORT_STOPPED);
        s_app_state = (ps == PLAYER_ERROR) ? APP_ERROR : APP_IDLE;
        if (ps == PLAYER_ERROR) {
          snprintf(s_error_msg, sizeof(s_error_msg), "Playback error occurred");
        }
      }
    }

    // ──── Render ────
    ui_begin_frame();

    switch (s_app_state) {
    case APP_IDLE:
      ui_draw_idle(network_get_ip());
      break;

    case APP_LOADING:
      ui_draw_loading(soap_get_current_uri());
      break;

    case APP_PLAYING:
    case APP_PAUSED:
      // Render video frame (fills the screen)
      player_render_frame();

      // Show overlay on button press or when paused
      if (s_app_state == APP_PAUSED || s_show_overlay) {
        ui_draw_playing_overlay(soap_get_current_uri(),
                                player_get_position_ms(),
                                player_get_duration_ms());
      }

      // Countdown overlay timer
      if (s_overlay_timer > 0) {
        s_overlay_timer--;
        if (s_overlay_timer <= 0) {
          s_show_overlay = 0;
        }
      }
      break;

    case APP_ERROR:
      ui_draw_error(s_error_msg);
      break;
    }

    ui_end_frame();

    // Small delay to prevent 100% CPU
    sceKernelDelayThread(1000); // 1ms
  }

  // ──── Cleanup ────
  ssdp_send_byebye();
  upnp_server_term();
  ssdp_term();
  player_term();
  hls_term();
  network_term();
  ui_term();

  sceKernelExitProcess(0);
  return 0;
}
