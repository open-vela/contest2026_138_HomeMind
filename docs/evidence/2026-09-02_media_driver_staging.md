# HomeMind media driver staging — 2026-09-02

## Scope

This is a source/configuration staging record. It is not a camera or microphone
hardware pass: the new image has not yet been built, flashed, and probed on the
ESP32-S3-EYE in this record.

## Hardware mapping used

- OV2640 DVP camera: XCLK GPIO15; PCLK GPIO13; VSYNC GPIO6; HREF GPIO7;
  D0..D7 = GPIO11, 9, 8, 10, 12, 18, 17, 16.
- Camera control bus: I2C0 SDA GPIO4, SCL GPIO5; sensor address 0x30.
- Onboard digital microphone: I2S0 master RX; BCLK GPIO41, WS GPIO42,
  DIN GPIO2; 16 kHz, 16-bit, mono-oriented input. MCLK is not enabled.
- LEDC channel 0 is explicitly routed to GPIO15 for the camera XCLK. The
  default LEDC pin GPIO2 would conflict with the microphone DIN signal.

## Source changes staged locally

- `firmware/nuttx_media/esp32s3_board_camera.c`: OV2640 register setup,
  ESP32-S3 LCD_CAM/V4L2 registration, and QVGA RGB565 capture descriptor.
- `firmware/patches/0002-homemind-esp32s3-eye-media.patch`: board Makefile,
  board header prototypes, and late bring-up calls for camera and I2S0.
- `tools/deploy-to-vm.sh`: installs the camera source and applies the media
  patch idempotently after the existing HomeMind NuttX patch.
- `scripts/build.sh`: enables the camera/I2S configuration and keeps the
  generic JPEG camera tool disabled until the raw RGB565-to-JPEG path is
  implemented and tested.

## Acceptance still required

1. Clean Ubuntu deployment and build with the media patch.
2. Flash the resulting image only after the build succeeds.
3. Serial probe: `/dev/video0`, `/dev/audio/pcm_in0`, camera I2C probe, and a
   real frame/audio read; retain failures as well as passes.
4. Add a verified RGB565-to-JPEG or sensor-JPEG path before enabling
   `camera_capture` → Vision LLM.
5. Feed real PCM into a genuine offline wake-word model. A cloud ASR keyword
   gate is not offline wake-up and will not be counted as such.

