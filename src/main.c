#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "dmi/dmi_pb.h"
#include "hw/dji_services.h"

#include "gfx.h"

#define MAGIC_HEADER 0x42069
#define KEY_DJI_BACK 0xc9

#define FRAME_TYPE_P 0x00
#define FRAME_TYPE_I 0x01
#define FRAME_TYPE_WAIT 0xFF

#define ACCEPTABLE_FRAME_TIME 50000000

typedef enum {
  STATE_CONNECT,
  STATE_HEADER_MAGIC,
  STATE_HEADER,
  STATE_DATA,
} state_t;

typedef struct connect_header_s {
  uint32_t magic;
  uint32_t width;
  uint32_t height;
  uint32_t fps;
} connect_header_t;

void do_net_accept();
void do_net_setup();
void get_connect_header_magic();
void get_connect_header();
void handle_data_exit();
void handle_data_setup();
void handle_data();
void usb_thread(int fd);

static int active_fd = -1;
static int usb_fd = -1;
static int watch_timer_fd = -1;
static int net_listen_fd, net_client_fd = -1;
static struct sockaddr_in net_client_addr;

struct timespec last_frame_time = {0, 0};

static dmi_pb_handle_t pb_handle;

static state_t state = STATE_CONNECT;
static connect_header_t connect_header;

char toast_buf[256];

uint8_t magic_buf[1024];
size_t magic_buf_pos = 0;

void get_connect_header_magic() {
  // Seek out magic bytes to sync alignment
  uint8_t magic_cur;
  uint32_t magic_expected = MAGIC_HEADER;

  uint8_t magic_expected_cur = (magic_expected >> (magic_buf_pos * 8)) & 0xFF;
  size_t recv_len = read(active_fd, &magic_cur, 1);
  if (recv_len == EAGAIN) {
    printf("Failed to get magic byte.\n");
    return;
  }

  printf("Got byte: %x, expected: %x\n", magic_cur, magic_expected_cur);
  if (magic_cur == magic_expected_cur) {
    magic_buf_pos++;
  } else {
    magic_buf_pos = 0;
  }

  if (magic_buf_pos == 4) {
    magic_buf_pos = 0;
    state = STATE_HEADER;
    connect_header.magic = MAGIC_HEADER;
  }
}

void get_connect_header() {
  size_t header_size_minus_magic = sizeof(connect_header) - sizeof(uint32_t);
  size_t recv_len = read(active_fd, magic_buf + magic_buf_pos,
                         header_size_minus_magic - magic_buf_pos);
  if (recv_len == EAGAIN) {
    return;
  }

  if (magic_buf_pos + recv_len < header_size_minus_magic) {
    magic_buf_pos += recv_len;
    return;
  }

  memcpy((void *)&connect_header + sizeof(uint32_t), magic_buf,
         header_size_minus_magic);

  sprintf(toast_buf, "%d x %d @ %d FPS", connect_header.width,
          connect_header.height, connect_header.fps);
  printf("Got connect header: %s\n", toast_buf);
  gfx_toast(toast_buf);

  magic_buf_pos = 0;
  state = STATE_DATA;

  handle_data_setup();
}

stream_in_header_t header;
uint8_t frame_buf[1000000];
uint32_t frame_size = 0;
uint32_t frame_pos = 0;
uint8_t frame_type = FRAME_TYPE_WAIT;

void handle_data_setup() {
  dmi_pb_start(&pb_handle, connect_header.width, connect_header.height,
               connect_header.fps);

  memset(&header, 0, sizeof(header));
  header.eof = 1;
  header.is_first_frm = 1;
  header.payload_offset = sizeof(header);
  header.pts = 0;

  frame_size = 0;
  frame_pos = 0;
  frame_type = 0xFF;
  clock_gettime(CLOCK_MONOTONIC, &last_frame_time);
}

void handle_data() {
  size_t recv_len = 0;

  if (frame_size == 0) {
    recv_len = read(active_fd, &frame_size, sizeof(frame_size));

    if (recv_len == 0) {
      printf("Connection closed\n");
      handle_data_exit();
      return;
    }

    if (recv_len != sizeof(frame_size)) {
      printf("Failed to get frame size\n");
      handle_data_exit();
      return;
    }

    if (frame_size > sizeof(frame_buf)) {
      printf("Frame size too big: %d (max: %d)\n", frame_size,
             sizeof(frame_buf));
      handle_data_exit();
      return;
    }

    frame_pos = 0;
    header.payload_lenth = frame_size;
  } else if (frame_type == FRAME_TYPE_WAIT) {
    recv_len = read(active_fd, &frame_type, sizeof(frame_type));

    if (recv_len == 0) {
      printf("Connection closed\n");
      handle_data_exit();
      return;
    }

    if (recv_len != sizeof(frame_type)) {
      printf("Failed to get frame type\n");
      handle_data_exit();
      return;
    }
  } else {
    recv_len = read(active_fd, frame_buf + frame_pos, frame_size - frame_pos);

    if (recv_len == 0) {
      printf("Connection closed\n");
      handle_data_exit();
      return;
    }

    frame_pos += recv_len;
    if (frame_pos < frame_size) {
      return;
    }

    // Calculate diff since last received frame.
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t diff = (now.tv_sec - last_frame_time.tv_sec) * 1000000000 +
                    (now.tv_nsec - last_frame_time.tv_nsec);
    last_frame_time = now;

    if (header.is_first_frm != 1 && diff >= ACCEPTABLE_FRAME_TIME &&
        frame_type != FRAME_TYPE_I) {
      printf("Dropping frame, %llums >= 50ms\n", diff / 1000000);
      sprintf(toast_buf, "Dropping frame, diff: %llums >= 50ms",
              diff / 1000000);
      gfx_toast(toast_buf);
    } else {
      dmi_pb_send(&pb_handle, &header, frame_buf, frame_size);
    }

    if (header.is_first_frm == 1) {
      gfx_splash_hide();
      header.is_first_frm = 0;
    }

    frame_size = 0;
    frame_type = FRAME_TYPE_WAIT;

    // Reset watchdog timer.
    struct itimerspec its = {
        .it_interval = {0, 0},
        .it_value = {3, 0},
    };
    timerfd_settime(watch_timer_fd, 0, &its, NULL);
  }
}

void handle_data_exit() {
  dmi_pb_stop(&pb_handle);
  gfx_splash_show();

  sprintf(toast_buf, "Connection lost!");
  gfx_toast(toast_buf);

  state = STATE_CONNECT;
  active_fd = -1;
}

void do_net_setup() {
  net_listen_fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in net_sockaddr;
  memset(&net_sockaddr, 0, sizeof(net_sockaddr));
  net_sockaddr.sin_family = AF_INET;
  net_sockaddr.sin_port = htons(42069);
  net_sockaddr.sin_addr.s_addr = inet_addr("0.0.0.0");

  bind(net_listen_fd, (struct sockaddr *)&net_sockaddr, sizeof(net_sockaddr));
  listen(net_listen_fd, 1);
}

void do_net_accept() {
  socklen_t net_client_addr_len = sizeof(net_client_addr);
  net_client_fd = accept(net_listen_fd, (struct sockaddr *)&net_client_addr,
                         &net_client_addr_len);

  sprintf(toast_buf, "Connection via RNDIS!");
  gfx_toast(toast_buf);
}

void usb_thread(int usb_pipe_fd_w) {
  // mfw usb-ffs can't into poll or non-blocking
  int usb_ep_fd = open("/dev/usb-ffs/bulk/ep1", O_RDONLY);

  while (1) {
    char buf[4096];
    int len = read(usb_ep_fd, buf, sizeof(buf));
    write(usb_pipe_fd_w, buf, len);
  };
}

int main(int argc, char *argv[]) {
  dmi_pb_init(&pb_handle);

  gfx_init();
  gfx_splash_show();

  int usb_pipe[2];
  pipe(usb_pipe);
  usb_fd = usb_pipe[0];
  pthread_t usb_thread_handle;
  pthread_create(&usb_thread_handle, NULL, usb_thread, (void *)usb_pipe[1]);

  int ep_fd = epoll_create1(0);
  struct epoll_event ep_ev;

  int input_fd = open("/dev/input/event0", O_RDONLY);
  ep_ev.events = EPOLLIN;
  ep_ev.data.fd = input_fd;
  epoll_ctl(ep_fd, EPOLL_CTL_ADD, input_fd, &ep_ev);

  ep_ev.events = EPOLLIN;
  ep_ev.data.fd = usb_fd;
  epoll_ctl(ep_fd, EPOLL_CTL_ADD, usb_fd, &ep_ev);

  do_net_setup();
  ep_ev.events = EPOLLIN;
  ep_ev.data.fd = net_listen_fd;
  epoll_ctl(ep_fd, EPOLL_CTL_ADD, ep_ev.data.fd, &ep_ev);

  watch_timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
  ep_ev.events = EPOLLIN;
  ep_ev.data.fd = watch_timer_fd;
  epoll_ctl(ep_fd, EPOLL_CTL_ADD, watch_timer_fd, &ep_ev);

  while (1) {
    int nfds = epoll_wait(ep_fd, &ep_ev, 1, 500);
    gfx_toast_tick();

    if (nfds == 0) {
      continue;
    }

    if (state == STATE_CONNECT) {
      if (ep_ev.data.fd == net_listen_fd) {
        do_net_accept();

        ep_ev.events = EPOLLIN;
        ep_ev.data.fd = net_client_fd;
        epoll_ctl(ep_fd, EPOLL_CTL_ADD, ep_ev.data.fd, &ep_ev);

        state = STATE_HEADER_MAGIC;
        active_fd = net_client_fd;
      } else if (ep_ev.data.fd == usb_fd) {
        sprintf(toast_buf, "Connection via BULK!");
        gfx_toast(toast_buf);

        state = STATE_HEADER_MAGIC;
        active_fd = usb_fd;
      }
    } else {
      if (ep_ev.data.fd == active_fd) {
        if (state == STATE_HEADER_MAGIC) {
          get_connect_header_magic();
        } else if (state == STATE_HEADER) {
          get_connect_header();
        } else if (state == STATE_DATA) {
          handle_data();
        }
      }
    }

    if (ep_ev.data.fd == watch_timer_fd) {
      uint64_t exp;
      read(watch_timer_fd, &exp, sizeof(exp));
      handle_data_exit();
    }

    if (ep_ev.data.fd == input_fd) {
      struct input_event ev;
      read(input_fd, &ev, sizeof(ev));

      if (ev.type == EV_KEY && ev.value == 1) {
        if (ev.code == KEY_DJI_BACK) {
          printf("Back button pressed, exiting\n");
          break;
        }
      }
    }
  }

  if (state == STATE_DATA) {
    handle_data_exit();
  }

  pthread_kill(usb_thread_handle, SIGKILL);
  dmi_pb_deinit(&pb_handle);

  return 0;
}
