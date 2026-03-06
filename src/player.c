#include "player.h"
#include "hls.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include <psp2/audioout.h>
#include <psp2/avplayer.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/http.h>
#include <psp2/sysmodule.h>
#include <vita2d.h>


// ──────────── Constants ────────────

#define MAX_HTTP_HANDLES 4
#define AUDIO_GRAIN 1024
#define GPU_MEM_ALIGN (256 * 1024)

#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

// ──────────── HTTP I/O Context ────────────

typedef struct {
  int tpl;
  int conn;
  char url[2048];
  uint64_t content_length;
  int in_use;
} HttpHandle;

static HttpHandle s_http_handles[MAX_HTTP_HANDLES];

// GPU memory block tracking
#define MAX_GPU_BLOCKS 16
typedef struct {
  SceUID uid;
  void *base;
  int in_use;
} GpuMemBlock;

static GpuMemBlock s_gpu_blocks[MAX_GPU_BLOCKS];

// ──────────── Player State ────────────

static SceAvPlayerHandle s_player_handle = 0;
static PlayerState s_player_state = PLAYER_IDLE;
static int s_audio_port = -1;
static SceUID s_audio_thread_uid = -1;
static volatile int s_audio_running = 0;
static vita2d_texture *s_video_texture = NULL;
static int s_video_width = 0;
static int s_video_height = 0;
static uint64_t s_current_position_ms = 0;
static uint64_t s_duration_ms = 0;
static int s_is_hls = 0;
static char s_current_url[2048] = {0};

// ──────────── Memory Callbacks ────────────

static void *player_mem_alloc(void *p, uint32_t alignment, uint32_t size) {
  (void)p;
  void *ptr = NULL;
  if (alignment < 16)
    alignment = 16;
  ptr = memalign(alignment, size);
  if (ptr)
    memset(ptr, 0, size);
  return ptr;
}

static void player_mem_free(void *p, void *ptr) {
  (void)p;
  free(ptr);
}

static void *player_gpu_alloc(void *p, uint32_t alignment, uint32_t size) {
  (void)p;
  if (alignment < GPU_MEM_ALIGN)
    alignment = GPU_MEM_ALIGN;
  size = ALIGN_UP(size, alignment);

  SceUID uid = sceKernelAllocMemBlock(
      "avplayer_gpu", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, size, NULL);
  if (uid < 0)
    return NULL;

  void *base = NULL;
  sceKernelGetMemBlockBase(uid, &base);

  // Track this allocation
  for (int i = 0; i < MAX_GPU_BLOCKS; i++) {
    if (!s_gpu_blocks[i].in_use) {
      s_gpu_blocks[i].uid = uid;
      s_gpu_blocks[i].base = base;
      s_gpu_blocks[i].in_use = 1;
      break;
    }
  }

  return base;
}

static void player_gpu_free(void *p, void *ptr) {
  (void)p;
  for (int i = 0; i < MAX_GPU_BLOCKS; i++) {
    if (s_gpu_blocks[i].in_use && s_gpu_blocks[i].base == ptr) {
      sceKernelFreeMemBlock(s_gpu_blocks[i].uid);
      s_gpu_blocks[i].in_use = 0;
      break;
    }
  }
}

// ──────────── File Replacement Callbacks (HTTP I/O) ────────────

static int player_file_open(void *p, const char *filename) {
  (void)p;

  // Find a free handle slot
  int slot = -1;
  for (int i = 0; i < MAX_HTTP_HANDLES; i++) {
    if (!s_http_handles[i].in_use) {
      slot = i;
      break;
    }
  }
  if (slot < 0)
    return -1;

  HttpHandle *h = &s_http_handles[slot];
  memset(h, 0, sizeof(*h));
  strncpy(h->url, filename, sizeof(h->url) - 1);

  // Create HTTP template
  h->tpl = sceHttpCreateTemplate("VitaReceiver/1.0", 2, 1);
  if (h->tpl < 0)
    return -1;

  // Disable SSL verification for HTTPS
  sceHttpsDisableOption(h->tpl, 0x01); // SCE_HTTPS_FLAG_SERVER_VERIFY

  // Create connection
  h->conn = sceHttpCreateConnectionWithURL(h->tpl, filename, 0);
  if (h->conn < 0) {
    sceHttpDeleteTemplate(h->tpl);
    return -1;
  }

  // Do a HEAD request to get Content-Length
  int req =
      sceHttpCreateRequestWithURL(h->conn, SCE_HTTP_METHOD_GET, filename, 0);
  if (req < 0) {
    sceHttpDeleteConnection(h->conn);
    sceHttpDeleteTemplate(h->tpl);
    return -1;
  }

  // Set range to just first byte to get Content-Length via Content-Range
  sceHttpAddRequestHeader(req, "Range", "bytes=0-0", SCE_HTTP_HEADER_OVERWRITE);
  sceHttpSetAutoRedirect(req, 1);

  int ret = sceHttpSendRequest(req, NULL, 0);
  if (ret < 0) {
    sceHttpDeleteRequest(req);
    sceHttpDeleteConnection(h->conn);
    sceHttpDeleteTemplate(h->tpl);
    return -1;
  }

  // Try to get content length from Content-Range header
  h->content_length = 0;
  char *content_range = NULL;
  unsigned int cr_len = 0;
  if (sceHttpGetAllResponseHeaders(req, &content_range, &cr_len) >= 0 &&
      content_range) {
    char *cr = strstr(content_range, "Content-Range:");
    if (!cr)
      cr = strstr(content_range, "content-range:");
    if (cr) {
      char *slash = strchr(cr, '/');
      if (slash) {
        h->content_length = (uint64_t)strtoull(slash + 1, NULL, 10);
      }
    }
  }

  // Fallback: try Content-Length header
  if (h->content_length == 0) {
    uint64_t cl = 0;
    if (sceHttpGetResponseContentLength(req, &cl) >= 0) {
      h->content_length = cl;
    }
  }

  // If still 0, set a large default (streaming)
  if (h->content_length == 0) {
    h->content_length = (uint64_t)2ULL * 1024 * 1024 * 1024; // 2GB default
  }

  sceHttpDeleteRequest(req);

  // Recreate connection for future reads (the old one may be in a bad state)
  sceHttpDeleteConnection(h->conn);
  h->conn = sceHttpCreateConnectionWithURL(h->tpl, filename, 0);

  h->in_use = 1;
  return slot;
}

static int player_file_close(void *p, int fd) {
  (void)p;
  if (fd < 0 || fd >= MAX_HTTP_HANDLES)
    return -1;

  HttpHandle *h = &s_http_handles[fd];
  if (!h->in_use)
    return -1;

  sceHttpDeleteConnection(h->conn);
  sceHttpDeleteTemplate(h->tpl);
  h->in_use = 0;

  return 0;
}

static int player_file_read(void *p, int fd, uint8_t *buffer, uint64_t position,
                            uint32_t length) {
  (void)p;
  if (fd < 0 || fd >= MAX_HTTP_HANDLES)
    return -1;

  HttpHandle *h = &s_http_handles[fd];
  if (!h->in_use)
    return -1;

  // Create a new request with Range header
  int req =
      sceHttpCreateRequestWithURL(h->conn, SCE_HTTP_METHOD_GET, h->url, 0);
  if (req < 0) {
    // Reconnect and retry
    sceHttpDeleteConnection(h->conn);
    h->conn = sceHttpCreateConnectionWithURL(h->tpl, h->url, 0);
    if (h->conn < 0)
      return -1;
    req = sceHttpCreateRequestWithURL(h->conn, SCE_HTTP_METHOD_GET, h->url, 0);
    if (req < 0)
      return -1;
  }

  char range[64];
  snprintf(range, sizeof(range), "bytes=%llu-%llu",
           (unsigned long long)position,
           (unsigned long long)(position + length - 1));
  sceHttpAddRequestHeader(req, "Range", range, SCE_HTTP_HEADER_OVERWRITE);
  sceHttpSetAutoRedirect(req, 1);

  int ret = sceHttpSendRequest(req, NULL, 0);
  if (ret < 0) {
    sceHttpDeleteRequest(req);
    return -1;
  }

  // Read data
  int total_read = 0;
  while ((uint32_t)total_read < length) {
    int read = sceHttpReadData(req, buffer + total_read, length - total_read);
    if (read <= 0)
      break;
    total_read += read;
  }

  sceHttpDeleteRequest(req);
  return total_read;
}

static uint64_t player_file_size(void *p, int fd) {
  (void)p;
  if (fd < 0 || fd >= MAX_HTTP_HANDLES)
    return 0;

  HttpHandle *h = &s_http_handles[fd];
  if (!h->in_use)
    return 0;

  return h->content_length;
}

// ──────────── Audio Thread ────────────

static int audio_thread_func(SceSize args, void *argp) {
  (void)args;
  (void)argp;

  s_audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, AUDIO_GRAIN,
                                     48000, SCE_AUDIO_OUT_MODE_STEREO);

  while (s_audio_running) {
    if (s_player_handle && s_player_state == PLAYER_PLAYING) {
      SceAvPlayerFrameInfo audio_info;
      memset(&audio_info, 0, sizeof(audio_info));

      if (sceAvPlayerGetAudioData(s_player_handle, &audio_info)) {
        int channels = audio_info.details.audio.channelCount;
        int sample_rate = audio_info.details.audio.sampleRate;

        sceAudioOutSetConfig(s_audio_port, AUDIO_GRAIN,
                             sample_rate > 0 ? sample_rate : 48000,
                             channels == 1 ? SCE_AUDIO_OUT_MODE_MONO
                                           : SCE_AUDIO_OUT_MODE_STEREO);

        sceAudioOutOutput(s_audio_port, audio_info.pData);
      } else {
        sceKernelDelayThread(5000); // 5ms
      }
    } else {
      sceKernelDelayThread(16000); // 16ms when not playing
    }
  }

  if (s_audio_port >= 0) {
    sceAudioOutReleasePort(s_audio_port);
    s_audio_port = -1;
  }

  return 0;
}

// ──────────── Public API ────────────

int player_init(void) {
  sceSysmoduleLoadModule(SCE_SYSMODULE_AVPLAYER);

  memset(s_http_handles, 0, sizeof(s_http_handles));
  memset(s_gpu_blocks, 0, sizeof(s_gpu_blocks));

  s_player_state = PLAYER_IDLE;
  s_video_texture = NULL;
  s_video_width = 0;
  s_video_height = 0;

  // Start audio thread
  s_audio_running = 1;
  s_audio_thread_uid = sceKernelCreateThread("audio_thread", audio_thread_func,
                                             0x10000100, 0x10000, 0, 0, NULL);
  if (s_audio_thread_uid >= 0) {
    sceKernelStartThread(s_audio_thread_uid, 0, NULL);
  }

  return 0;
}

int player_play(const char *url) {
  // Stop any current playback
  if (s_player_handle) {
    player_stop();
  }

  strncpy(s_current_url, url, sizeof(s_current_url) - 1);
  s_current_url[sizeof(s_current_url) - 1] = '\0';
  s_player_state = PLAYER_LOADING;
  s_current_position_ms = 0;
  s_duration_ms = 0;

  // Check if HLS
  s_is_hls = hls_is_playlist(url);

  if (s_is_hls) {
    // Load HLS playlist and play first segment
    hls_init();
    int count = hls_load_playlist(url);
    if (count <= 0) {
      s_player_state = PLAYER_ERROR;
      return -1;
    }

    const char *seg_url = hls_get_next_segment();
    if (!seg_url) {
      s_player_state = PLAYER_ERROR;
      return -1;
    }

    // Download first segment
    const char *local_path = hls_download_segment(seg_url);
    if (!local_path) {
      s_player_state = PLAYER_ERROR;
      return -1;
    }

    // Play local file (no HTTP file replacement needed)
    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    init.memoryReplacement.allocate = player_mem_alloc;
    init.memoryReplacement.deallocate = player_mem_free;
    init.memoryReplacement.allocateTexture = player_gpu_alloc;
    init.memoryReplacement.deallocateTexture = player_gpu_free;
    init.basePriority = 0xA0;
    init.numOutputVideoFrameBuffers = 2;
    init.autoStart = 1;

    s_player_handle = sceAvPlayerInit(&init);
    if (!s_player_handle) {
      s_player_state = PLAYER_ERROR;
      return -1;
    }

    sceAvPlayerAddSource(s_player_handle, local_path);
  } else {
    // Direct HTTP playback with file replacement callbacks
    SceAvPlayerInitData init;
    memset(&init, 0, sizeof(init));
    init.memoryReplacement.allocate = player_mem_alloc;
    init.memoryReplacement.deallocate = player_mem_free;
    init.memoryReplacement.allocateTexture = player_gpu_alloc;
    init.memoryReplacement.deallocateTexture = player_gpu_free;
    init.fileReplacement.objectPointer = NULL;
    init.fileReplacement.open = player_file_open;
    init.fileReplacement.close = player_file_close;
    init.fileReplacement.readOffset = player_file_read;
    init.fileReplacement.size = player_file_size;
    init.basePriority = 0xA0;
    init.numOutputVideoFrameBuffers = 2;
    init.autoStart = 1;

    s_player_handle = sceAvPlayerInit(&init);
    if (!s_player_handle) {
      s_player_state = PLAYER_ERROR;
      return -1;
    }

    sceAvPlayerAddSource(s_player_handle, url);
  }

  s_player_state = PLAYER_PLAYING;
  return 0;
}

int player_pause(void) {
  if (s_player_handle && s_player_state == PLAYER_PLAYING) {
    sceAvPlayerPause(s_player_handle);
    s_player_state = PLAYER_PAUSED;
  }
  return 0;
}

int player_resume(void) {
  if (s_player_handle && s_player_state == PLAYER_PAUSED) {
    sceAvPlayerResume(s_player_handle);
    s_player_state = PLAYER_PLAYING;
  }
  return 0;
}

int player_stop(void) {
  if (s_player_handle) {
    sceAvPlayerStop(s_player_handle);
    sceAvPlayerClose(s_player_handle);
    s_player_handle = 0;
  }

  if (s_video_texture) {
    vita2d_free_texture(s_video_texture);
    s_video_texture = NULL;
  }

  s_video_width = 0;
  s_video_height = 0;
  s_player_state = PLAYER_STOPPED;
  s_current_position_ms = 0;
  s_duration_ms = 0;

  if (s_is_hls) {
    hls_term();
    s_is_hls = 0;
  }

  return 0;
}

int player_render_frame(void) {
  if (!s_player_handle || s_player_state != PLAYER_PLAYING)
    return 0;

  // Check if player is still active
  if (!sceAvPlayerIsActive(s_player_handle)) {
    if (s_is_hls) {
      // Try next HLS segment
      const char *seg_url = hls_get_next_segment();
      if (seg_url) {
        const char *local_path = hls_download_segment(seg_url);
        if (local_path) {
          // Close current player and start new one for next segment
          sceAvPlayerClose(s_player_handle);

          SceAvPlayerInitData init;
          memset(&init, 0, sizeof(init));
          init.memoryReplacement.allocate = player_mem_alloc;
          init.memoryReplacement.deallocate = player_mem_free;
          init.memoryReplacement.allocateTexture = player_gpu_alloc;
          init.memoryReplacement.deallocateTexture = player_gpu_free;
          init.basePriority = 0xA0;
          init.numOutputVideoFrameBuffers = 2;
          init.autoStart = 1;

          s_player_handle = sceAvPlayerInit(&init);
          if (s_player_handle) {
            sceAvPlayerAddSource(s_player_handle, local_path);
            return 0;
          }
        }
      }
    }
    // Playback finished or failed
    s_player_state = PLAYER_STOPPED;
    return 0;
  }

  // Get video frame
  SceAvPlayerFrameInfo video_info;
  memset(&video_info, 0, sizeof(video_info));

  if (sceAvPlayerGetVideoData(s_player_handle, &video_info)) {
    int w = video_info.details.video.width;
    int h = video_info.details.video.height;

    // Update timestamp
    s_current_position_ms = video_info.timeStamp / 1000; // us to ms

    // Create or recreate texture if dimensions changed
    if (!s_video_texture || s_video_width != w || s_video_height != h) {
      if (s_video_texture) {
        vita2d_free_texture(s_video_texture);
      }
      s_video_texture = vita2d_create_empty_texture_format(
          w, h, SCE_GXM_TEXTURE_FORMAT_A8B8G8R8);
      s_video_width = w;
      s_video_height = h;
    }

    if (s_video_texture && video_info.pData) {
      // Copy frame data to texture
      void *tex_data = vita2d_texture_get_datap(s_video_texture);
      unsigned int stride = vita2d_texture_get_stride(s_video_texture);
      unsigned int row_bytes = w * 4;

      if (stride == row_bytes) {
        memcpy(tex_data, video_info.pData, w * h * 4);
      } else {
        for (int y = 0; y < h; y++) {
          memcpy((uint8_t *)tex_data + y * stride,
                 video_info.pData + y * row_bytes, row_bytes);
        }
      }

      // Draw video scaled to fit screen (960x544)
      float scale_x = 960.0f / (float)w;
      float scale_y = 544.0f / (float)h;
      float scale = (scale_x < scale_y) ? scale_x : scale_y;
      float draw_x = (960.0f - (float)w * scale) / 2.0f;
      float draw_y = (544.0f - (float)h * scale) / 2.0f;

      vita2d_draw_texture_scale(s_video_texture, draw_x, draw_y, scale, scale);
      return 1;
    }
  }

  // No new frame, draw the last one if available
  if (s_video_texture) {
    float scale_x = 960.0f / (float)s_video_width;
    float scale_y = 544.0f / (float)s_video_height;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;
    float draw_x = (960.0f - (float)s_video_width * scale) / 2.0f;
    float draw_y = (544.0f - (float)s_video_height * scale) / 2.0f;

    vita2d_draw_texture_scale(s_video_texture, draw_x, draw_y, scale, scale);
    return 1;
  }

  return 0;
}

int player_is_active(void) {
  if (!s_player_handle)
    return 0;
  return sceAvPlayerIsActive(s_player_handle) ? 1 : 0;
}

PlayerState player_get_state(void) { return s_player_state; }

uint64_t player_get_position_ms(void) { return s_current_position_ms; }

uint64_t player_get_duration_ms(void) { return s_duration_ms; }

void player_term(void) {
  player_stop();

  // Stop audio thread
  s_audio_running = 0;
  if (s_audio_thread_uid >= 0) {
    sceKernelWaitThreadEnd(s_audio_thread_uid, NULL, NULL);
    sceKernelDeleteThread(s_audio_thread_uid);
    s_audio_thread_uid = -1;
  }

  // Free any remaining GPU blocks
  for (int i = 0; i < MAX_GPU_BLOCKS; i++) {
    if (s_gpu_blocks[i].in_use) {
      sceKernelFreeMemBlock(s_gpu_blocks[i].uid);
      s_gpu_blocks[i].in_use = 0;
    }
  }

  sceSysmoduleUnloadModule(SCE_SYSMODULE_AVPLAYER);
}
