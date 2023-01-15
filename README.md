# dji-moonlight-shim

Stream games via Moonlight to your DJI FPV Goggles. Counterpart to
[dji-moonlight-embedded](https://github.com/Knifa/dji-moonlight-embedded).

![splash](assets/splash.png)

## Implementation

Technically, this will accept any Annex B H.264 stream. The shim hosts a TCP
server on port `42069` that accepts a single connection at a time.

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

## License

- DejaVu Sans, used for the toast font: see [LICENSE-DejaVuSans](assets/LICENSE-DejaVuSans).
- Moonlight logo: see [LICENSE-Moonlight](assets/LICENSE-Moonlight).

Everything else: see [LICENSE](LICENSE).
