#include "ssdp.h"
#include "network.h"
#include "upnp_server.h"


#include <stdio.h>
#include <string.h>


#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/rtc.h>


static int s_ssdp_sock = -1;
static uint64_t s_last_notify_tick = 0;

// Format an SSDP response/notify message
static int format_ssdp_response(char *buf, int bufsize, const char *st,
                                int is_notify) {
  const char *ip = network_get_ip();

  if (is_notify) {
    return snprintf(buf, bufsize,
                    "NOTIFY * HTTP/1.1\r\n"
                    "HOST: " SSDP_MULTICAST_ADDR ":%d\r\n"
                    "CACHE-CONTROL: max-age=1800\r\n"
                    "LOCATION: http://%s:%d/device.xml\r\n"
                    "NT: %s\r\n"
                    "NTS: ssdp:alive\r\n"
                    "SERVER: VitaOS/1.0 UPnP/1.0 VitaReceiver/1.0\r\n"
                    "USN: " UPNP_DEVICE_UDN "::%s\r\n"
                    "\r\n",
                    SSDP_PORT, ip, UPNP_HTTP_PORT, st, st);
  } else {
    return snprintf(buf, bufsize,
                    "HTTP/1.1 200 OK\r\n"
                    "CACHE-CONTROL: max-age=1800\r\n"
                    "EXT:\r\n"
                    "LOCATION: http://%s:%d/device.xml\r\n"
                    "SERVER: VitaOS/1.0 UPnP/1.0 VitaReceiver/1.0\r\n"
                    "ST: %s\r\n"
                    "USN: " UPNP_DEVICE_UDN "::%s\r\n"
                    "\r\n",
                    ip, UPNP_HTTP_PORT, st, st);
  }
}

static void send_notify_alive(void) {
  if (s_ssdp_sock < 0)
    return;

  SceNetSockaddrIn mcast_addr;
  memset(&mcast_addr, 0, sizeof(mcast_addr));
  mcast_addr.sin_family = SCE_NET_AF_INET;
  mcast_addr.sin_port = sceNetHtons(SSDP_PORT);
  sceNetInetPton(SCE_NET_AF_INET, SSDP_MULTICAST_ADDR, &mcast_addr.sin_addr);

  char buf[1024];

  // Notify root device
  const char *targets[] = {"upnp:rootdevice",
                           "urn:schemas-upnp-org:device:MediaRenderer:1",
                           "urn:schemas-upnp-org:service:AVTransport:1",
                           "urn:schemas-upnp-org:service:ConnectionManager:1"};

  for (int i = 0; i < 4; i++) {
    int len = format_ssdp_response(buf, sizeof(buf), targets[i], 1);
    sceNetSendto(s_ssdp_sock, buf, len, 0, (SceNetSockaddr *)&mcast_addr,
                 sizeof(mcast_addr));
    sceKernelDelayThread(50 * 1000); // 50ms between packets
  }
}

static int match_search_target(const char *st) {
  if (strstr(st, "ssdp:all"))
    return 1;
  if (strstr(st, "upnp:rootdevice"))
    return 1;
  if (strstr(st, "urn:schemas-upnp-org:device:MediaRenderer"))
    return 1;
  if (strstr(st, "urn:schemas-upnp-org:service:AVTransport"))
    return 1;
  if (strstr(st, "urn:schemas-upnp-org:service:ConnectionManager"))
    return 1;
  return 0;
}

int ssdp_init(void) {
  // Create UDP socket
  s_ssdp_sock = sceNetSocket("ssdp", SCE_NET_AF_INET, SCE_NET_SOCK_DGRAM, 0);
  if (s_ssdp_sock < 0)
    return s_ssdp_sock;

  // Allow address reuse
  int reuse = 1;
  sceNetSetsockopt(s_ssdp_sock, SCE_NET_SOL_SOCKET, SCE_NET_SO_REUSEADDR,
                   &reuse, sizeof(reuse));

  // Set non-blocking
  network_set_nonblocking(s_ssdp_sock);

  // Bind to SSDP port
  SceNetSockaddrIn addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = SCE_NET_AF_INET;
  addr.sin_port = sceNetHtons(SSDP_PORT);
  addr.sin_addr.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);

  int ret = sceNetBind(s_ssdp_sock, (SceNetSockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    sceNetSocketClose(s_ssdp_sock);
    s_ssdp_sock = -1;
    return ret;
  }

  // Join multicast group
  SceNetIpMreq mreq;
  sceNetInetPton(SCE_NET_AF_INET, SSDP_MULTICAST_ADDR, &mreq.imr_multiaddr);
  mreq.imr_interface.s_addr = sceNetHtonl(SCE_NET_INADDR_ANY);

  sceNetSetsockopt(s_ssdp_sock, SCE_NET_IPPROTO_IP, SCE_NET_IP_ADD_MEMBERSHIP,
                   &mreq, sizeof(mreq));

  // Send initial NOTIFY
  send_notify_alive();

  SceRtcTick tick;
  sceRtcGetCurrentTick(&tick);
  s_last_notify_tick = tick.tick;

  return 0;
}

void ssdp_poll(void) {
  if (s_ssdp_sock < 0)
    return;

  // Check for M-SEARCH requests
  char recv_buf[2048];
  SceNetSockaddrIn from_addr;
  unsigned int from_len = sizeof(from_addr);

  int received = sceNetRecvfrom(s_ssdp_sock, recv_buf, sizeof(recv_buf) - 1, 0,
                                (SceNetSockaddr *)&from_addr, &from_len);

  if (received > 0) {
    recv_buf[received] = '\0';

    // Check if it's an M-SEARCH request
    if (strstr(recv_buf, "M-SEARCH") != NULL) {
      // Extract ST header
      char *st_line = strstr(recv_buf, "ST:");
      if (!st_line)
        st_line = strstr(recv_buf, "st:");
      if (!st_line)
        st_line = strstr(recv_buf, "St:");

      if (st_line && match_search_target(st_line)) {
        // Small delay to avoid congestion
        sceKernelDelayThread(100 * 1000); // 100ms

        char response[1024];

        // Respond with MediaRenderer
        int len = format_ssdp_response(
            response, sizeof(response),
            "urn:schemas-upnp-org:device:MediaRenderer:1", 0);
        sceNetSendto(s_ssdp_sock, response, len, 0,
                     (SceNetSockaddr *)&from_addr, from_len);

        // Also respond with AVTransport
        len = format_ssdp_response(response, sizeof(response),
                                   "urn:schemas-upnp-org:service:AVTransport:1",
                                   0);
        sceNetSendto(s_ssdp_sock, response, len, 0,
                     (SceNetSockaddr *)&from_addr, from_len);
      }
    }
  }

  // Periodic NOTIFY alive
  SceRtcTick now;
  sceRtcGetCurrentTick(&now);
  uint64_t elapsed_us = now.tick - s_last_notify_tick;
  if (elapsed_us >= (uint64_t)SSDP_NOTIFY_INTERVAL * 1000000) {
    send_notify_alive();
    s_last_notify_tick = now.tick;
  }
}

void ssdp_send_byebye(void) {
  if (s_ssdp_sock < 0)
    return;

  SceNetSockaddrIn mcast_addr;
  memset(&mcast_addr, 0, sizeof(mcast_addr));
  mcast_addr.sin_family = SCE_NET_AF_INET;
  mcast_addr.sin_port = sceNetHtons(SSDP_PORT);
  sceNetInetPton(SCE_NET_AF_INET, SSDP_MULTICAST_ADDR, &mcast_addr.sin_addr);

  char buf[1024];
  int len = snprintf(buf, sizeof(buf),
                     "NOTIFY * HTTP/1.1\r\n"
                     "HOST: " SSDP_MULTICAST_ADDR ":%d\r\n"
                     "NT: urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
                     "NTS: ssdp:byebye\r\n"
                     "USN: " UPNP_DEVICE_UDN
                     "::urn:schemas-upnp-org:device:MediaRenderer:1\r\n"
                     "\r\n",
                     SSDP_PORT);

  sceNetSendto(s_ssdp_sock, buf, len, 0, (SceNetSockaddr *)&mcast_addr,
               sizeof(mcast_addr));
}

void ssdp_term(void) {
  if (s_ssdp_sock >= 0) {
    ssdp_send_byebye();
    sceNetSocketClose(s_ssdp_sock);
    s_ssdp_sock = -1;
  }
}
