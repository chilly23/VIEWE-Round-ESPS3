MUSE OFFLINE / VIEWE ROUND

Connect the board and double-click Flash.cmd. It detects the ESP32 USB COM
port, checks firmware hashes and uploads the ready-built demo. No rebuild,
Muse app, Wi-Fi, SDK token or account is needed to use the demo.
This replaces the currently installed firmware and its partition layout.

WHAT YOU GET
Actual official Muse UI and procedural character code from:
https://github.com/facebookincubator/muse-gadget-sdk
Base commit: 4268880f4bf3e9c736c08ea5efeac6f628fe6f97
UI/input updated from: 8e613f77f3a95a63e70cf8be549db9e589105ea0
The demo banner and instructional captions have been removed.

The official round interface, fonts, avatar, state colours, progress ring,
audio-level animation, pet reaction, captions, and swipe settings are retained.
It uses our VIEWE-ROUND-V1.1 CO5300 display/touch board port.
This is an unofficial offline demo built from that source, not a connected
Muse service. Listening/speaking levels and replies are scripted; it does
not record speech, generate AI replies, or play synthesized speech.

CONTROLS
- On boot: official wake-up animation, then the official idle character (no automatic state cycling).
- Hold the on-screen microphone or BOOT: simulated listening. Release: thinking, then a scripted reply.
- Tap the character: the official happy/petting reaction.
- Swipe left: original settings pages. Swipe right: return to character.
- Brightness works locally. Networking/account pages have no backend here.
- Automatic screen sleep is disabled at each startup for this offline build.

FILES
- Flash.cmd: ready-built firmware uploader for this Windows PC.
- firmware/: bootloader, partition table, OTA initialization and application.
- source.zip: complete source and license; extract, then run
  muse-board\Build Offline Demo.cmd to rebuild using installed ESP-IDF 6.0.1.
- preview/: previews from the actual official character renderer; these are
  software previews, not photographs or screenshots from the physical board.
- manifest.json: firmware hashes and validation status.

Local editable source: C:\Users\Admin\Downloads\muse-board
Offline logic: esp32\main\muse_offline.c
UI: esp32\components\muse\muse_ui.c
Character artwork/animation: esp32\avatar\muse_pixel.c
Board wiring: esp32\components\muse\boards\board_viewe_s3_round.c

The source and upstream assets retain their original copyright and license.
See LICENSE and SOURCE-NOTES.txt for changes and provenance.
