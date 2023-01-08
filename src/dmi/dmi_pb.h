#pragma once

#include "duss_media.h"
#include "shram.h"

typedef struct dmi_pb_handle {
  int media_playback_channel;
  int media_control_channel;
  shram_handle_t shram_handle;
} dmi_pb_handle_t;

void dmi_pb_init(dmi_pb_handle_t *handle);
void dmi_pb_deinit(dmi_pb_handle_t *handle);

void dmi_pb_start(dmi_pb_handle_t *handle, uint32_t width, uint32_t height,
                  uint32_t fps);
void dmi_pb_stop(dmi_pb_handle_t *handle);

void dmi_pb_send(dmi_pb_handle_t *handle, stream_in_header_t *header,
                 uint8_t *data, size_t size);
