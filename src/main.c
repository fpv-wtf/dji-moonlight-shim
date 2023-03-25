#include <arpa/inet.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "dmi/dmi_pb.h"
#include "hw/dji_services.h"

#include "gfx.h"

#define MAGIC_HEADER 0x42069

static FILE *usb_file = NULL;
static FILE *net_file = NULL;

static int net_listen_fd = 0;
static struct sockaddr_in net_client_addr;
static bool use_net = false;

static dmi_pb_handle_t pb_handle;

typedef struct connect_header_s {
  uint32_t magic;
  uint32_t width;
  uint32_t height;
  uint32_t fps;
} connect_header_t;

static connect_header_t connect_header;

void handle_client(FILE *client_file) {
  size_t recv_len;

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

  uint8_t frame_buf[1000000];
  uint32_t frame_size = 0;

  while (1) {
    size_t recv_len = 0;

    if (frame_size == 0) {
      recv_len = fread(&frame_size, sizeof(frame_size), 1, client_file);
      // printf("Got frame size: %d\n", frame_size);

      if (recv_len == 0) {
        printf("Connection closed\n");
        break;
      }

      if (recv_len != 1) {
        printf("Failed to get frame size\n");
        return;
      }

      if (frame_size > sizeof(frame_buf)) {
        printf("Frame size too big: %d (max: %d)\n", frame_size,
               sizeof(frame_buf));
        return;
      }
    } else {
      recv_len = fread(frame_buf, frame_size, 1, client_file);
      // printf("Got frame data (%d).\n", recv_len);

      if (recv_len == 0) {
        printf("Connection error\n");
        break;
      }

      header.payload_lenth = frame_size;
      dmi_pb_send(&pb_handle, &header, frame_buf, frame_size);

      if (header.is_first_frm == 1) {
        gfx_splash_hide();
        header.is_first_frm = 0;
      }

      gfx_toast_tick();

      frame_size = 0;
    }
  }

  dmi_pb_stop(&pb_handle);
  gfx_splash_show();
}

void do_net() {
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
      net_file = fdopen(client_fd, "r+");

      char ip_str[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &(net_client_addr.sin_addr), ip_str, INET_ADDRSTRLEN);

      sprintf(toast_buf, "Connection from %s!", ip_str);
      gfx_toast(toast_buf);

      memset(&connect_header, 0, sizeof(connect_header));
      size_t recv_len =
          recv(client_fd, &connect_header, sizeof(connect_header), MSG_WAITALL);

      if (recv_len != sizeof(connect_header)) {
        printf("Failed to get connect header\n");
      } else if (connect_header.magic != 0x42069) {
        printf("Invalid connect header magic\n");
      } else {
        handle_client(net_file);
      }

      close(client_fd);
      fclose(net_file);
      client_fd = -1;
      net_file = NULL;

      sprintf(toast_buf, "Connection lost!");
      gfx_toast(toast_buf);
    }
  }
}

void do_usb() {
  usb_file = fopen("/dev/usb-ffs/bulk/ep1", "r");

  printf("Waiting for alignment...\n");

  // Seek out magic bytes to sync alignment
  uint8_t magic_cur;
  uint32_t magic_expected = MAGIC_HEADER;
  size_t magic_pos = 0;
  while (magic_pos != sizeof(magic_expected)) {
    uint8_t magic_expected_cur = (magic_expected >> (magic_pos * 8)) & 0xFF;
    fread(&magic_cur, sizeof(magic_cur), 1, usb_file);
    printf("Got byte: %x, expected: %x\n", magic_cur, magic_expected_cur);
    if (magic_cur == magic_expected_cur) {
      magic_pos++;
    } else {
      magic_pos = 0;
    }
  }

  printf("Got alignment!\n");

  memset(&connect_header, 0, sizeof(connect_header));
  connect_header.magic = MAGIC_HEADER;

  size_t recv_len =
      fread((void *)&connect_header + sizeof(uint32_t),
            sizeof(connect_header) - sizeof(uint32_t), 1, usb_file);

  handle_client(usb_file);
}

int main(int argc, char *argv[]) {
  if (argc > 1) {
    if (strcmp(argv[1], "--net") == 0) {
      use_net = true;
    } else if (strcmp(argv[1], "--usb") == 0) {
      use_net = false;
    } else {
      printf("Usage: %s [--net|--usb] (default usb)\n", argv[0]);
      return 1;
    }
  }

  dmi_pb_init(&pb_handle);

  gfx_init();
  gfx_splash_show();

  if (use_net) {
    printf("Using network mode\n");
    gfx_toast("Using network mode...");
    gfx_toast_tick();

    do_net();
  } else {
    printf("Using USB mode\n");
    gfx_toast("Using USB mode...");
    gfx_toast_tick();

    do_usb();
  }

  dmi_pb_deinit(&pb_handle);

  return 0;
}
