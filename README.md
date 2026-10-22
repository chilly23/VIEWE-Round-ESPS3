# VIEWE Round ESP32-S3 projects

Firmware, Arduino sketches, board adaptations, and reference material for the VIEWE round ESP32-S3 LCD/AMOLED board.

## Projects

- `02_lvgl_demo_v9`: LVGL widgets demo.
- `03_esp_brookesia`: Brookesia launcher and applications.
- `04_Immersive_block`: interactive shapes demo.
- `05_Spec_Analyzer`: spectrum analyzer display. Microphone input is currently documented as not working; see `VIEWE-Demos/README.txt`.
- `esp-mosaico-fluidbox-main`: FluidBox port with touch and accelerometer input.
- `VIEWE-Demos`: shared BSP, bundled dependency sources, demo launchers, and validation records. Keep this alongside the ESP-IDF demo directories.
- `muse-board`: Muse device SDK sources and VIEWE board port. See its README and START-HERE guide for prerequisites and provisioning.
- `Muse-Offline-VIEWE`: standalone offline Muse UI demo, flashing tools, firmware, previews, and its source archive.
- `round_animations_co5300`, `round_generative_art`, `round_loader_animations`, `round_url_display`, `viewe_touch_rings`: Arduino sketches and related notes.
- `img`: board photos and video.

## Getting started

For the five ESP-IDF demos, start with `VIEWE-Demos/README.txt` and `VIEWE-Demos/Start.cmd`. Follow each project's build instructions; the local demo workflow uses ESP-IDF 6.0.1. For the standalone offline UI, see `Muse-Offline-VIEWE/README.txt`. Arduino sketches have their own library and board requirements.

Prepared demo firmware is retained where supplied. Flashing replaces the running firmware; read the relevant project's instructions first. Existing validation records describe earlier local testing, not a fresh hardware test of this upload.

## Repository contents

This source snapshot excludes generated build trees, downloaded managed components, Python dependencies, caches, logs, editor state, local SDK tokens, private firmware packages, and original device flash backups. Shared and vendored source components are retained because the projects reference them. Dependency lockfiles and `sdkconfig.defaults` are retained; generated local `sdkconfig` files are excluded.

The `muse-board` directory is included as ordinary source files rather than a nested Git repository. Original upstream licenses, notices, and attribution remain with their projects. See individual license files for applicable terms.

Muse includes its upstream shared development signing key because its default build requires it. This is explicitly documented in `muse-board/esp32/sdkconfig.defaults`; use a separate private release key for production.
