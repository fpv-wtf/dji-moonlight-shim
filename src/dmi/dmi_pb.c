#include "dmi_pb.h"
#include "dji_media.h"
#include "duss_media.h"
#include "shram.h"
#include <string.h>

static uint8_t *mangle_extradata(uint8_t *extradata, size_t extradata_size,
                                 size_t *new_extradata_size);
static uint32_t get_nalu_size(uint8_t nalu_len, uint8_t *frame);
static void *mangle_frame(uint8_t *frame, size_t frame_size,
                          size_t *new_frame_size);

void dmi_pb_init(dmi_pb_handle_t *handle) {
  shram_open(&handle->shram_handle);

  handle->media_control_channel = dji_open_media_control_channel();
  handle->media_playback_channel = dji_open_media_playback_channel();
}

void dmi_pb_deinit(dmi_pb_handle_t *handle) { dmi_pb_stop(handle); }

void dmi_pb_start(dmi_pb_handle_t *handle, uint32_t width, uint32_t height,
                  uint32_t fps) {
  shram_set_u32(&handle->shram_handle, SHRAM_OFFSET_RATE_NUM, fps);
  shram_set_u32(&handle->shram_handle, SHRAM_OFFSET_RATE_DEN, 1);
  shram_set_u64(&handle->shram_handle, SHRAM_OFFSET_AUDIO_PTS_MAYBE, 0);
  shram_set_u8(&handle->shram_handle, SHRAM_OFFSET_PAUSE, 0);

  dji_send_media_command(handle->media_control_channel, DUSS_MEDIA_CMD_PB_START,
                         width | height << 0x10);
}

void dmi_pb_stop(dmi_pb_handle_t *handle) {
  stream_in_header_t header;
  memset(&header, 0, sizeof(header));
  header.eof = 1;
  header.eos = 1;
  dmi_pb_send(handle, &header, NULL, 0);

  dji_send_media_command(handle->media_control_channel, DUSS_MEDIA_CMD_PB_STOP,
                         0);
}

void dmi_pb_send(dmi_pb_handle_t *handle, stream_in_header_t *header,
                 uint8_t *data, size_t size) {
  io_pkt_handle_t pkt_handle;

  dji_claim_io_pkt(handle->media_playback_channel, &pkt_handle);

  memcpy(pkt_handle.data, header, sizeof(stream_in_header_t));
  memcpy(pkt_handle.data + sizeof(stream_in_header_t), data, size);

  dji_release_io_pkt(handle->media_playback_channel, &pkt_handle,
                     sizeof(stream_in_header_t) + size);
}
