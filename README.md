# dji-moonlight-shim

Stream games via Moonlight and [fpv.wtf](https://github.com/fpv-wtf) to your DJI
FPV Goggles!

![splash](media/logo.png)

The DJI Moonlight project is made up of three parts:

- **[dji-moonlight-shim](https://github.com/fpv-wtf/dji-moonlight-shim)**: a
  goggle-side app that displays a video stream coming in over USB. _You are
  here._
- **[dji-moonlight-gui](https://github.com/fpv-wtf/dji-moonlight-gui)**: a
  Windows app that streams games to the shim via Moonlight and friends.
- [dji-moonlight-embedded](https://github.com/fpv-wtf/dji-moonlight-embedded): a
  fork of Moonlight Embedded that can stream to the shim. The GUI app uses this
  internally.

Latency is good, in the 7-14ms range at 120Hz (w/ 5900X + 3080Ti via GeForce
Experience).

![latency](media/latency.gif)

## Usage

### Setup

1. Go to [fpv.wtf](https://fpv.wtf/) with your goggles connected and powered up.
2. Update WTFOS to the latest version.
3. Install
   [dji-moonlight-shim](https://fpv.wtf/package/fpv-wtf/dji-moonlight-shim) via
   the package mangaer.
4. Continue to [dji-moonlight-gui](https://github.com/fpv-wtf/dji-moonlight-gui)
   for PC-side setup.

### Running

1. Connect your goggles to your PC via USB.
2. Select `Moonlight` from the menu.

   ![menu](media/menu.jpg)

3. The shim will start and wait for a connection.
4. Use [dji-moonlight-gui](https://github.com/fpv-wtf/dji-moonlight-gui) to
   stream video from your PC.
5. Press the BACK button on the goggles to exit the shim at any time.

## Implementation

Technically, this will accept any Annex B H.264 stream since all this does
really is pipe the stream right into the decoder.

First, it expects a connect header:

```c
struct connect_header_s {
  uint32_t magic; // 0x42069
  uint32_t width;
  uint32_t height;
  uint32_t fps;
}
```

After which, it expects a stream of frames. Each frame should be sent prefixed
with a `uint32_t` length header, followed by the frame data itself.

The maximum frame size is `1MB (1000000 bytes)`, which is slightly under the
maximum packet size for the decoder. Any larger and this would need to handle
packet splitting. But also, this is absolutely enormous for a single frame.

### RNDIS

For RNDIS, the shim hosts a TCP server on port `42069` that accepts a single
connection at a time. Normal client/server stuff applies here.

### BULK

For BULK, the shim reads from the FunctionFS bulk endpoint already setup by DJI
at `/dev/usb-ffs/bulk/ep1`. FunctionFS does not support non-blocking IO nor does
it support polling so reading is done from a thread into a pipe.

Currently, there's no way to tell if the BULK side has connected or
disconnected, so on startup the shim just waits for the magic number to appear
in this file, at which point it assumes it's about to get the rest of the
connect header, followed by the rest of the stream. After that, we rely on a
watchdog timer to detect if the connection has been lost (i.e., no data received
for some seconds).

### Booting

The `dji-moonlight-shim` that gets popped into `/opt/bin` is actually a wrapper
around the actual binary in `/opt/moonlight/`. The glasses service and the shim
can't co-exist so this wrapper handles stopping (and restarting) it.

### Decoder

Everything around decoding lives in [dmi](./src/dmi) and is probably the best
way to understand how this works. Start from [dmi_pb.c](./src/dmi/dmi_pb.c).

It's driven through a handful of devices via ioctl/iomap:

- `/dev/dmi_media_control`: general control, starting/stoping the decoder, etc.
- `/dev/dmi_video_playback`: the place where frames go.
- `/dev/mem`: general shared mem, mainly just for frame timing info here though.

The setup is roughly:

1. Send a media playback command to start the decoder, with the expected frame
   dimensions.
2. Set the frame rate and unpause the decoder, directly via shared memory.

Then for each frame you want to decode:

1. Claim packets via the `dmi_video_playback` device, which gives you a chunk of
   shared memory to write the packet to.
2. Write the frame data along with a header to the shared memory.
3. Release the packet. This will trigger the decoder to decode the frame.

## License

- DejaVu Sans, used for the toast font: see
  [LICENSE-DejaVuSans](assets/LICENSE-DejaVuSans).
- Moonlight logo: see [LICENSE-Moonlight](assets/LICENSE-Moonlight).

Everything else: see [LICENSE](LICENSE).
