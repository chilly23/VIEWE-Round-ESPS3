# Muse on your VIEWE board

This is a separate copy of the official Muse Gadget SDK with a new board port
for **VIEWE-ROUND-V1.1 / 202506**, using the **466 x 466 CO5300 AMOLED**.
The old `agent-gadget` project is untouched.

**Build status:** compiled successfully; physical board testing is still pending.
The ready firmware is in `esp32/build-muse-viewe-s3-round/`. See `VALIDATION.md`.

## On this Windows PC

1. Your SDK token is in the private `.sdk-token` file. To replace it, double-click
   **Set Token.cmd**. Never upload that file or the token-bearing build directory.
2. Double-click **Build VIEWE.cmd** to compile after any changes.
3. Connect the VIEWE with a USB data cable. Double-click **Flash VIEWE.cmd** and
   enter its USB COM port. This replaces the application on the board.
4. Open the Muse phone app, enable **Settings > Devices > Developer mode**, then
   **Add Device**. Select the `MuseGadget-XXXXXX` shown on the screen.
5. Press **BOOT** when pairing asks for physical confirmation. Finish Wi-Fi setup
   in the app. Hold BOOT to talk once connected; use the touchscreen for settings.

If flashing cannot connect, hold BOOT, tap RST, release BOOT, and try again.
If powered off, use the board's PWR button to turn it on.

For a serial log in PowerShell, replace COM5 with your board's actual port:

```powershell
powershell -ExecutionPolicy Bypass -File .\viewe.ps1 monitor -Port COM5
```

## What this port includes

- Muse's existing animated avatar, BLE pairing, Wi-Fi and voice conversation.
- CO5300 QSPI display, round layout, brightness and display sleep.
- Automatic detection of CST9217 (0x5A) or CST816/820 (0x15) touch.
- I2S MEMS microphone and NS4168 speaker on their separate I2S buses.
- BOOT for pairing and push-to-talk. The physical PWR circuit stays hardware controlled.
- Local USB firmware updates. Automatic OTA is disabled for this custom port.

The shared V1.1 PCB also accepts other displays. This port covers its CO5300
AMOLED variants, not the 1.85-inch TFT. The previous session identified your
panel as AMOLED; the two new image attachments did not load here. If the screen
stays black, confirm the panel variant before substituting unrelated pin maps.

RTC, IMU, rotary encoder rotation, SD card and battery percentage are not
implemented. Power-off in the screen menu means deep sleep; it does not operate
the board's hardware power circuit. Press BOOT to wake from deep sleep.

Display offset defaults to 6 columns. If a panel revision is shifted or touch is
mirrored, run `viewe.ps1 menuconfig` and change the **Muse > VIEWE** options.
The microphone slot defaults to left; a right-slot option is available there too.

## Files

- `esp32/components/muse/boards/board_viewe_s3_round.c`: board driver and pins.
- `esp32/devices/sdkconfig.muse-viewe-s3-round`: memory and board configuration.
- `viewe.ps1` / `run-viewe.py`: Windows build, flash and monitor commands.
- `viewe-build.log`: build output, with SDK token strings redacted.
- `VALIDATION.md`: what has actually been checked.

ESP-IDF **v6.0.1** and its tools are installed in your user profile on this PC.
Another machine needs that toolchain installed before using these scripts.
On Linux/macOS, activate the same IDF version and run `python3 run-viewe.py build`.

## Sources

- [Official Muse Gadget SDK](https://github.com/facebookincubator/muse-gadget-sdk),
  based on commit `4268880f4bf3e9c736c08ea5efeac6f628fe6f97`.
- [VIEWE manufacturer firmware](https://github.com/VIEWESMART/ESP32-S3-Round-Dev-Board),
  commit `5cff3a10df37a556a849b1f8a89d59912316ffa5`, particularly `viewe1.5`
  and `viewe1.75` under `examples/esp-idf/ESP32-S3-Round-Dev-Board/`.
- The manufacturer's V1.1 schematic identifies the mic as MSM261D4030H1CPM;
  its sample driver calls it INMP441. The port uses the shared I2S interface.
  Speaker timing follows the included NS4168 datasheet section 10.2.

Upstream licenses and avatar notices are retained. This is a local board port,
not an official claim of hardware certification or successful physical testing.
