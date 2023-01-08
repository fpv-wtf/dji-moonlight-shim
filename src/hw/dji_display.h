#pragma once

#include "duml_hal.h"

typedef struct dji_display_state_s {
  duss_hal_obj_handle_t disp_handle;
  duss_hal_obj_handle_t ion_handle;
  duss_disp_instance_handle_t *disp_instance_handle;
  uint8_t is_v2_goggles;
} dji_display_state_t;

typedef struct dji_display_plane_s {
  duss_disp_plane_id_t plane_id;
  duss_hal_mem_handle_t ion_buf;
  duss_frame_buffer_t fb;
  void *fb_physical_addr;
  void *fb_virtual_addr;
} dji_display_plane_t;

void dji_display_init(dji_display_state_t *display_state);
void dji_display_deinit(dji_display_state_t *display_state);

void dji_display_plane_init(dji_display_state_t *display_state,
                            dji_display_plane_t *plane,
                            duss_disp_plane_id_t plane_id);
void dji_display_plane_push_frame(dji_display_state_t *display_state,
                                  dji_display_plane_t *plane);
