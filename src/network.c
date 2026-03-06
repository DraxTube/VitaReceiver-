#include "network.h"
#include <stdio.h>
#include <string.h>

#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

#define NET_MEMORY_SIZE (1 * 1024 * 1024)
#define HTTP_MEMORY_SIZE (1 * 1024 * 1024)

static char s_ip_string[32];
static uint32_t s_ip_addr;
static int s_initialized = 0;

int network_init(void) {
  int ret;

  // Load required modules
  sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
  sceSysmoduleLoadModule(SCE_SYSMODULE_HTTP);
  sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);

  // Initialize networking
  SceNetInitParam net_param;
  static char net_memory[NET_MEMORY_SIZE];
  net_param.memory = net_memory;
  net_param.size = NET_MEMORY_SIZE;
  net_param.flags = 0;

  ret = sceNetInit(&net_param);
  if (ret < 0 && ret != 0x80410005) { // Ignore already initialized
    return ret;
  }

  ret = sceNetCtlInit();
  if (ret < 0 && ret != 0x80412102) {
    return ret;
  }

  // Get IP address
  SceNetCtlInfo info;
  ret = sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &info);
  if (ret < 0) {
    snprintf(s_ip_string, sizeof(s_ip_string), "0.0.0.0");
    s_ip_addr = 0;
  } else {
    snprintf(s_ip_string, sizeof(s_ip_string), "%s", info.ip_address);
    sceNetInetPton(SCE_NET_AF_INET, info.ip_address, &s_ip_addr);
  }

  // Note: SSL is handled internally by SceHttp when SCE_SYSMODULE_SSL is loaded

  // Initialize HTTP
  ret = sceHttpInit(HTTP_MEMORY_SIZE);
  if (ret < 0 && ret != 0x80431002) {
    // Non-fatal
  }

  s_initialized = 1;
  return 0;
}

const char *network_get_ip(void) { return s_ip_string; }

uint32_t network_get_ip_uint(void) { return s_ip_addr; }

int network_set_nonblocking(int sock) {
  int val = 1;
  return sceNetSetsockopt(sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_NBIO, &val,
                          sizeof(val));
}

void network_term(void) {
  if (!s_initialized)
    return;

  sceHttpTerm();
  sceNetCtlTerm();
  sceNetTerm();

  sceSysmoduleUnloadModule(SCE_SYSMODULE_SSL);
  sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTP);
  sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);

  s_initialized = 0;
}
