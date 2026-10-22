# VIEWE port validation — 2026-10-13

## Passed

- Full ESP32-S3 firmware build using ESP-IDF v6.0.1, exit code 0.
- New `board_viewe_s3_round.c` compiled and linked into the firmware.
- Signed application: **2,101,248 bytes** (`0x201000`), within the 4,194,304-byte
  application slot. **2,093,056 bytes (about 50%) remain free.**
- Bootloader: 22,608 bytes; its size check also passed.
- All 25 settings in the VIEWE board overlay were applied to generated sdkconfig.
- Actual C touch decoder executed in a host test with 11 assertions: both packet
  formats, press/release, panel edges, out-of-range positions and unknown address.
- PowerShell and Bash launcher syntax checks; Python helper syntax checks.
- Source scan confirms the private SDK token is absent from packaged source files.
- Secure Boot enforcement, flash encryption and eFuse pairing authentication are
  disabled. The existing public development signing key is retained from upstream.

## Hardware cross-check

Pins were checked against the manufacturer's firmware and V1.1 schematic.
GPIO45 drives the optional TFT backlight and is not required by the AMOLED.
The microphone is on I2S0 (BCLK 1, WS 42, data 2); speaker is on a separate I2S1
(BCLK 5, WS 6, data 4, enable 7). The schematic's mic is MSM261D4030H1CPM with
L/R grounded; the vendor names its compatible sample driver `inmp441`.

The NS4168 datasheet specifies standard Philips I2S with a one-clock delay.
This port follows that timing instead of the vendor example's MSB/left-justified
slot macro. Muse supplies the same audio sample to both speaker slots, so CTRL
high selecting the NS4168's right channel is supported.

## Not verified

- **No device was flashed.** USB enumeration found no USB serial device.
- Display operation/orientation, touch alignment, microphone level, speaker sound,
  Wi-Fi/BLE pairing and live Muse replies still need a physical test.
- The latest image attachments did not load. The PCB label was confirmed by the
  user; CO5300 AMOLED identification comes from the earlier saved session.
- The complete upstream host-test suite is **not green/verified on this Windows
  setup**. Its initial run reported 392 tests, 89 errors and 30 skips, including
  missing desktop compilers and platform modules. A compiler-enabled retry was
  stopped during a lengthy upstream C++ sanitizer build to finish the firmware
  compilation. The new board's touch test passed separately.
- Other board profiles were not rebuilt. This port adds a separate board choice;
  it does not change their drivers or the shared Muse application logic.

Build output: `viewe-first-build.log` (SDK token values redacted).
Your token is embedded in the compiled app, so the private firmware bundle and
build directory should not be shared. The source ZIP excludes both.
