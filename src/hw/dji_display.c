#include "dji_display.h"
#include <stdlib.h>

#define GOGGLES_V1_VOFFSET 575
#define GOGGLES_V2_VOFFSET 215

static duss_result_t pop_func(duss_disp_instance_handle_t *disp_handle,
                              duss_disp_plane_id_t plane_id,
                              duss_frame_buffer_t *frame_buffer,
                              void *user_ctx) {
  return 1;
}

void dji_display_close_framebuffer(dji_display_state_t *display_state) {
  // duss_hal_display_port_enable(display_state->disp_instance_handle, 3, 0);
  // duss_hal_display_release_plane(display_state->disp_instance_handle,
  // display_state->plane_id);
  // duss_hal_display_close(display_state->disp_handle,
  // &display_state->disp_instance_handle);
  // duss_hal_mem_free(display_state->ion_buf_0);
  // duss_hal_mem_free(display_state->ion_buf_1);
  // duss_hal_device_close(display_state->disp_handle);
  // duss_hal_device_stop(display_state->ion_handle);
  // duss_hal_device_close(display_state->ion_handle);
  // duss_hal_deinitialize();
}

void dji_display_init(dji_display_state_t *display_state) {
  uint32_t hal_device_open_unk = 0;
  duss_result_t res = 0;

  duss_hal_device_desc_t device_descs[3] = {
      {"/dev/dji_display", &duss_hal_attach_disp, &duss_hal_detach_disp, 0x0},
      {"/dev/ion", &duss_hal_attach_ion_mem, &duss_hal_detach_ion_mem, 0x0},
      {0, 0, 0, 0}};

  duss_hal_initialize(device_descs);

  res = duss_hal_device_open("/dev/dji_display", &hal_device_open_unk,
                             &display_state->disp_handle);
  if (res != 0) {
    printf("failed to open dji_display device");
    exit(0);
  }

  res = duss_hal_display_open(display_state->disp_handle,
                              &display_state->disp_instance_handle, 0);
  if (res != 0) {
    printf("failed to open display hal");
    exit(0);
  }

  res = duss_hal_display_reset(display_state->disp_instance_handle);
  if (res != 0) {
    printf("failed to reset display");
    exit(0);
  }

  res = duss_hal_display_port_enable(display_state->disp_instance_handle, 3, 1);
  if (res != 0) {
    printf("failed to enable display port");
    exit(0);
  }

  res = duss_hal_device_open("/dev/ion", &hal_device_open_unk,
                             &display_state->ion_handle);
  if (res != 0) {
    printf("failed to open shared VRAM");
    exit(0);
  }

  res = duss_hal_device_start(display_state->ion_handle, 0);
  if (res != 0) {
    printf("failed to start VRAM device");
    exit(0);
  }
}

void dji_display_plane_init(dji_display_state_t *display_state,
                            dji_display_plane_t *plane,
                            duss_disp_plane_id_t plane_id) {
  duss_result_t res = 0;
  uint8_t acquire_plane_mode = display_state->is_v2_goggles ? 6 : 0;

  plane->plane_id = plane_id;

  res = duss_hal_display_aquire_plane(display_state->disp_instance_handle,
                                      acquire_plane_mode, &plane_id);
  if (res != 0) {
    printf("failed to acquire plane");
    exit(0);
  }

  res = duss_hal_display_register_frame_cycle_callback(
      display_state->disp_instance_handle, plane_id, &pop_func, 0);
  if (res != 0) {
    printf("failed to register callback");
    exit(0);
  }

  duss_disp_plane_blending_t blending;
  blending.is_enable = 0;
  blending.voffset = GOGGLES_V1_VOFFSET;
  blending.hoffset = 0;
  blending.order = plane_id;
  blending.glb_alpha_en = 0;
  blending.glb_alpha_val = 0;
  blending.blending_alg = 0;

  res = duss_hal_display_plane_blending_set(display_state->disp_instance_handle,
                                            plane_id, &blending);
  if (res != 0) {
    printf("failed to set blending");
    exit(0);
  }

  res = duss_hal_mem_alloc(display_state->ion_handle, &plane->ion_buf, 0x473100,
                           0x400, 0, 0x17);
  if (res != 0) {
    printf("failed to allocate VRAM");
    exit(0);
  }

  res = duss_hal_mem_map(plane->ion_buf, &plane->fb_virtual_addr);
  if (res != 0) {
    printf("failed to map VRAM");
    exit(0);
  }

  res = duss_hal_mem_get_phys_addr(plane->ion_buf, &plane->fb_physical_addr);
  if (res != 0) {
    printf("failed to get FB0 phys addr");
    exit(0);
  }

  plane->fb.buffer = plane->ion_buf;
  plane->fb.pixel_format = display_state->is_v2_goggles
                               ? DUSS_PIXFMT_RGBA8888_GOGGLES_V2
                               : DUSS_PIXFMT_RGBA8888;

  plane->fb.width = 1440;
  plane->fb.height = 810;

  plane->fb.planes[0].bytes_per_line = 0x1680;
  plane->fb.planes[0].offset = 0;
  plane->fb.planes[0].plane_height = 810;
  plane->fb.planes[0].bytes_written = 0x473100;
  plane->fb.plane_count = 1;
}

void dji_display_plane_push_frame(dji_display_state_t *display_state,
                                  dji_display_plane_t *plane) {
  duss_hal_mem_sync(plane->fb.buffer, 1);
  duss_hal_display_push_frame(display_state->disp_instance_handle,
                              plane->plane_id, &plane->fb);
}
