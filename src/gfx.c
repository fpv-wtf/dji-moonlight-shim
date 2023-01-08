#include <ctype.h>
#include <png.h>
#include <stdlib.h>
#include <string.h>

#include "hw/dji_display.h"

const uint32_t WIDTH = 1440;
const uint32_t HEIGHT = 810;
const uint32_t BPP = 4;
const uint32_t DISPLAY_SIZE = WIDTH * HEIGHT * BPP;

static dji_display_state_t *display_state = NULL;
static dji_display_plane_t *plane_bg = NULL;
static dji_display_plane_t *plane_fg = NULL;

typedef struct font_char_s {
  uint32_t id;
  uint16_t x;
  uint16_t y;
  uint16_t width;
  uint16_t height;
  uint16_t x_offset;
  uint16_t y_offset;
  uint16_t x_advance;
  uint8_t page;
  uint8_t chnl;
} font_char_t;

static font_char_t *font_map = NULL;
static size_t font_map_count = 0;
static void *font_map_buf = NULL;
static uint32_t font_map_width = 0;
static uint16_t font_line_height = 0;

typedef struct toast_s {
  char text[256];
  struct toast_s *next;
  struct timespec expire;
} toast_t;

static toast_t *toast_queue = NULL;

static void gfx_load_font();
static void gfx_toast_draw();

void gfx_init() {
  display_state = malloc(sizeof(dji_display_state_t));
  memset(display_state, 0, sizeof(dji_display_state_t));
  display_state->is_v2_goggles = 1;

  plane_bg = malloc(sizeof(dji_display_plane_t));
  memset(plane_bg, 0, sizeof(dji_display_plane_t));

  plane_fg = malloc(sizeof(dji_display_plane_t));
  memset(plane_fg, 0, sizeof(dji_display_plane_t));

  // display_state = dji_display_state_alloc(1);
  // dji_display_open_framebuffer(display_state, 0);

  dji_display_init(display_state);
  dji_display_plane_init(display_state, plane_bg, 1);
  dji_display_plane_init(display_state, plane_fg, 2);

  memset(plane_bg->fb_virtual_addr, 0, DISPLAY_SIZE);
  memset(plane_fg->fb_virtual_addr, 0, DISPLAY_SIZE);

  gfx_load_font();
}

void gfx_deinit() {
  // dji_display_close_framebuffer(display_state);
  // dji_display_state_free(display_state);
}

void gfx_splash_show() {
  png_image image;
  memset(&image, 0, sizeof(image));
  image.version = PNG_IMAGE_VERSION;

  png_image_begin_read_from_file(&image, "./assets/splash.png");
  image.format = PNG_FORMAT_BGRA;

  void *buf = malloc(PNG_IMAGE_SIZE(image));
  png_image_finish_read(&image, NULL, buf, 0, NULL);

  void *fb = plane_bg->fb_virtual_addr;

  for (uint32_t y = 0; y < HEIGHT; y++) {
    for (uint32_t x = 0; x < WIDTH; x++) {
      uint32_t offset = (y * WIDTH + x) * BPP;
      uint32_t *buf_pixel = (uint32_t *)(buf + offset);
      uint32_t *fb_pixel = (uint32_t *)(fb + offset);

      *fb_pixel = *buf_pixel & 0xFFFFFFFF;
    }
  }

  dji_display_plane_push_frame(display_state, plane_bg);
  free(buf);
}

void gfx_splash_hide() {
  void *fb = plane_bg->fb_virtual_addr;

  for (uint32_t y = 0; y < HEIGHT; y++) {
    for (uint32_t x = 0; x < WIDTH; x++) {
      uint32_t offset = (y * WIDTH + x) * BPP;
      uint32_t *fb_pixel = (uint32_t *)(fb + offset);

      *fb_pixel = 0x00000000;
    }
  }

  dji_display_plane_push_frame(display_state, plane_bg);
}

void gfx_toast(char *text) {
  toast_t *toast = malloc(sizeof(toast_t));
  strlcpy(toast->text, text, sizeof(toast->text));
  toast->next = NULL;

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  toast->expire.tv_nsec = now.tv_nsec;
  toast->expire.tv_sec = now.tv_sec + 3;

  if (toast_queue == NULL) {
    toast_queue = toast;
  } else {
    toast_t *last = toast_queue;
    while (last->next != NULL) {
      last = last->next;
    }
    last->next = toast;
  }

  gfx_toast_draw();
}

void gfx_toast_clear() {
  memset(plane_fg->fb_virtual_addr, 0, DISPLAY_SIZE);
  dji_display_plane_push_frame(display_state, plane_fg);
}

void gfx_toast_tick() {
  if (toast_queue == NULL) {
    return;
  }

  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  int need_redraw = 0;
  for (toast_t *toast = toast_queue; toast != NULL; toast = toast->next) {
    if (now.tv_sec > toast->expire.tv_sec) {
      toast_queue = toast->next;
      free(toast);
      need_redraw = 1;
    }
  }

  if (need_redraw) {
    gfx_toast_draw();
  }
}

static void gfx_load_font() {
  png_image image;
  memset(&image, 0, sizeof(image));
  image.version = PNG_IMAGE_VERSION;

  png_image_begin_read_from_file(&image, "./assets/dejavusans.png");
  image.format = PNG_FORMAT_BGRA;
  font_map_width = image.width;

  font_map_buf = malloc(PNG_IMAGE_SIZE(image));
  png_image_finish_read(&image, NULL, font_map_buf, 0, NULL);

  FILE *font_desc = fopen("./assets/dejavusans.fnt", "r");
  fseek(font_desc, 0, SEEK_SET);

  uint8_t magic[4];
  fread(magic, sizeof(uint8_t), 4, font_desc);
  if (strncmp((char *)magic, "BMF\3", 4) != 0) {
    printf("Invalid font file!\n");
    exit(1);
  }

  uint8_t block_type;
  uint32_t size;
  fread(&block_type, sizeof(uint8_t), 1, font_desc);
  fread(&size, sizeof(uint32_t), 1, font_desc);
  if (block_type != 1) {
    printf("Couldn't find font info.\n");
    exit(1);
  }
  fseek(font_desc, size, SEEK_CUR);

  fread(&block_type, sizeof(uint8_t), 1, font_desc);
  fread(&size, sizeof(uint32_t), 1, font_desc);
  if (block_type != 2) {
    printf("Couldn't find common info.\n");
    exit(1);
  }
  fread(&font_line_height, sizeof(uint16_t), 1, font_desc);
  fseek(font_desc, size - sizeof(uint16_t), SEEK_CUR);

  fread(&block_type, sizeof(uint8_t), 1, font_desc);
  fread(&size, sizeof(uint32_t), 1, font_desc);
  if (block_type != 3) {
    printf("Couldn't find page info.\n");
    exit(1);
  }
  fseek(font_desc, size, SEEK_CUR);

  fread(&block_type, sizeof(uint8_t), 1, font_desc);
  fread(&size, sizeof(uint32_t), 1, font_desc);
  if (block_type != 4) {
    printf("Couldn't find chars info.\n");
    exit(1);
  }

  font_map_count = size / 20;
  font_map = calloc(font_map_count, sizeof(font_char_t));

  for (uint32_t i = 0; i < font_map_count; i++) {
    fread(&font_map[i], sizeof(font_char_t), 1, font_desc);
    // printf("Loaded char %c (%d)\n", font_map[i].id, font_map[i].id);
  }

  fclose(font_desc);
}

static void gfx_toast_draw() {
  gfx_toast_clear();

  uint32_t toast_x = 32;
  uint32_t toast_y = 32;

  for (toast_t *toast = toast_queue; toast != NULL; toast = toast->next) {
    char *text = toast->text;
    size_t char_count = strlen(text);

    uint32_t fb_x_offset = 0;
    uint32_t fb_y_offset = 0;

    for (size_t i = 0; i < char_count; i++) {
      char c = text[i];

      if (c == '\n') {
        fb_x_offset = 0;
        fb_y_offset += font_line_height;
        continue;
      }

      font_char_t *font_char = NULL;
      for (size_t j = 0; j < font_map_count; j++) {
        if (font_map[j].id == c) {
          font_char = &font_map[j];
          break;
        }
      }

      if (font_char == NULL) {
        continue;
      }

      fb_x_offset += font_char->x_offset;

      for (uint32_t y = 0; y < font_char->height; y++) {
        if (toast_y + fb_y_offset + y >= HEIGHT) {
          continue;
        }

        for (uint32_t x = 0; x < font_char->width; x++) {
          if (toast_x + fb_x_offset + x >= WIDTH) {
            continue;
          }

          uint32_t fb_offset = ((toast_y + fb_y_offset + y) * WIDTH +
                                (toast_x + fb_x_offset + x)) *
                               BPP;
          uint32_t *fb_pixel =
              (uint32_t *)(plane_fg->fb_virtual_addr + fb_offset);

          uint32_t font_offset =
              ((font_char->y + y) * font_map_width + (font_char->x + x)) * BPP;
          uint32_t *font_pixel = (uint32_t *)(font_map_buf + font_offset);

          *fb_pixel = *font_pixel & 0xFFFFFFFF;
        }
      }

      fb_x_offset += font_char->x_advance;
    }

    toast_y += fb_y_offset + font_line_height;
  }
}
