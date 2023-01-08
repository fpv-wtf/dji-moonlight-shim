#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "dmi/dmi_pb.h"
#include "hw/dji_services.h"

#include "gfx.h"

static int net_listen_fd = 0;
static struct sockaddr_in net_client_addr;
static dmi_pb_handle_t pb_handle;

typedef struct connect_header_s {
  uint32_t magic;
  uint32_t width;
  uint32_t height;
  uint32_t fps;
} connect_header_t;

void handle_client(int client_fd) {
  connect_header_t connect_header;
  memset(&connect_header, 0, sizeof(connect_header));
  size_t recv_len =
      recv(client_fd, &connect_header, sizeof(connect_header), MSG_WAITALL);

  if (recv_len != sizeof(connect_header)) {
    printf("Failed to get connect header\n");
    return;
  }

  if (connect_header.magic != 0x42069) {
    printf("Invalid connect header magic\n");
    return;
  }

  char toast_buf[256];
  sprintf(toast_buf, "%d x %d @ %d FPS", connect_header.width,
          connect_header.height, connect_header.fps);
  printf("Got connect header: %s\n", toast_buf);
  gfx_toast(toast_buf);

  dmi_pb_start(&pb_handle, connect_header.width, connect_header.height,
               connect_header.fps);

  stream_in_header_t header;
  memset(&header, 0, sizeof(header));
  header.eof = 1;
  header.is_first_frm = 1;
  header.payload_offset = sizeof(header);
  header.pts = 0;

  uint8_t frame_buf[20000];
  uint32_t frame_size = 0;
  uint32_t frame_offset = 0;

  while (1) {
    size_t recv_len = 0;

    if (frame_size == 0) {
      recv_len = recv(client_fd, &frame_size, sizeof(frame_size), MSG_WAITALL);

      if (recv_len == 0) {
        printf("Connection closed\n");
        break;
      }

      if (recv_len != sizeof(frame_size)) {
        printf("Failed to get frame size\n");
        frame_size = 0;
      }

      if (frame_size > sizeof(frame_buf)) {
        printf("Frame size too big\n");
        frame_size = 0;
      }
    } else {
      recv_len = recv(client_fd, frame_buf + frame_offset, frame_size, 0);
      frame_offset += recv_len;

      if (recv_len == 0) {
        printf("Connection closed\n");
        break;
      }

      if (frame_offset < frame_size) {
        continue;
      }

      header.payload_lenth = frame_size;
      dmi_pb_send(&pb_handle, &header, frame_buf, frame_size);

      if (header.is_first_frm == 1) {
        gfx_splash_hide();
        header.is_first_frm = 0;
      }

      gfx_toast_tick();

      frame_size = 0;
      frame_offset = 0;
    }
  }

  dmi_pb_stop(&pb_handle);
  gfx_splash_show();
}

int main(int argc, char *argv[]) {
  dji_stop_goggles(dji_goggles_are_v2());
  usleep(1000000);

  dmi_pb_init(&pb_handle);

  gfx_init();
  gfx_splash_show();

  net_listen_fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in net_sockaddr;
  memset(&net_sockaddr, 0, sizeof(net_sockaddr));
  net_sockaddr.sin_family = AF_INET;
  net_sockaddr.sin_port = htons(42069);
  net_sockaddr.sin_addr.s_addr = inet_addr("0.0.0.0");

  bind(net_listen_fd, (struct sockaddr *)&net_sockaddr, sizeof(net_sockaddr));
  listen(net_listen_fd, 1);

  struct pollfd poll_fd[1];
  poll_fd[0].fd = net_listen_fd;
  poll_fd[0].events = POLLIN;

  while (1) {
    char toast_buf[256];

    poll(poll_fd, 1, 500);
    gfx_toast_tick();

    if (poll_fd[0].revents & POLLIN) {
      socklen_t net_client_addr_len = sizeof(net_client_addr);
      int client_fd = accept(net_listen_fd, (struct sockaddr *)&net_client_addr,
                             &net_client_addr_len);

      char ip_str[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(net_client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);

      sprintf(toast_buf, "Connection from %s!", ip_str);
      gfx_toast(toast_buf);

      handle_client(client_fd);

      sprintf(toast_buf, "Connection lost!");
      gfx_toast(toast_buf);
    }
  }

  dmi_pb_deinit(&pb_handle);

  return 0;
}
