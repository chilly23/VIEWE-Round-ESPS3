VIEWE ROUND - FIVE SEPARATE DEMO PROJECTS

Double-click Start.cmd to choose a demo. Only one firmware runs at a time.
Each project also has Flash.cmd and Build.cmd. Flash.cmd uses the prebuilt
firmware when available; Build.cmd rebuilds the editable source with ESP-IDF 6.0.1
and refreshes that project's prebuilt firmware. Keep the USB cable connected.
No Muse account, token or phone app is required for any of these demos.
If uploading cannot connect: hold BOOT, tap RST, release BOOT, then retry.
The current firmware is replaced when you choose a different demo.

Projects in Downloads:
  02_lvgl_demo_v9
  03_esp_brookesia
  04_Immersive_block
  05_Spec_Analyzer
  esp-mosaico-fluidbox-main

Keep VIEWE-Demos alongside the four LVGL projects: they share the VIEWE BSP
and public LVGL/driver dependency sources here to avoid duplicating them.
FluidBox is independent and includes its own board/display/IMU adaptation.
The original ZIPs are unchanged in D:\ZIPS.

HARDWARE
VIEWE-ROUND-V1.1: ESP32-S3, 16MB flash, 8MB OPI PSRAM, 466x466 CO5300.
QSPI CS12 CLK48 D0=13 D1=47 D2=21 D3=14 RESET11; panel X offset 6.
Touch I2C SDA17 SCL18 RESET10 INT9; CST816/820@0x15 confirmed on this board.
QMI8658 accelerometer confirmed at 0x6A. Display rotated 180 degrees, with
touch coordinates matched to the display. FluidBox steering is calibrated
using physical feedback from this board.
BOOT GPIO0. Microphone BCLK1 WS42 DATA2, left I2S slot, Philips timing.
The Waveshare PMIC, expander and audio-codec initialization is not run.

FLUIDBOX
Starts in water mode. Tap BOOT to cycle particles/liquid/water/lava/ukiyoe.
Hold BOOT about 0.8 seconds to reset particles. Drag on the screen to steer.
If a QMI8658 is positively identified, accelerometer tilt is also enabled.
The original Mosaico BMI270 driver and ESP32-S31 display driver are retained
as reference files but excluded from the VIEWE build.
The renderer uses a 464x464 viewport with a small black margin on the panel,
an 8-pixel water sampling grid and internal DMA bounce buffers for ESP32-S3.
No performance figures from the original ESP32-S31 board apply to this port.
Verified on the physical board: animated water, upright orientation, touch
steering and accelerometer tilt; user confirmed both directions correct.

IMMERSIVE BLOCK
Uses a positively identified QMI8658 when available; otherwise touch steers
the original shapes demo. BOOT recalibrates; release the screen during calibration.

BROOKESIA
Includes the bundled launcher and Squareline app. The 480px theme is adapted
to 466px, including the recent-app preview. The UI task has a 32KB stack to
support the nested rendering that overflowed the default 8KB task stack.

SPECTRUM ANALYZER
Display builds and runs, but audio input is NOT working on this board yet.
The VIEWE GPIO1/42/2 microphone stream returned zeros in both mono and stereo
capture tests; the bars therefore do not react to sound. This limitation is
recorded in VALIDATION.json. No artificial/demo audio is substituted.

BACKUP
backup\before-demos-2026-10-16.bin contains the private, complete original
16MB flash image. Keep it private: it may contain old device settings.
It is not included in public source/firmware packages.

See VALIDATION.json for actual build/flash/boot results. A successful boot
log does not prove the physical screen, touch, microphone or IMU works.
