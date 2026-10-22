# VIEWE touch rings

Open **viewe_touch_rings.ino** in Arduino IDE. All firmware code is in that one file; you can also copy its entire contents into a new sketch.

This recreates the cyan/yellow Nawsome Uiverse SVG supplied in the request, using native vector drawing on the ESP32. Hold your finger anywhere on the screen to spread, rotate, and skew the layers. Release to return. The rings keep their original 7 / 3 / 4 / 2 second alternating rotation timings and a 0.5 second touch transition. Hold BOOT while the sketch is running to preview the touch effect.

## Arduino IDE setup

1. In **File > Preferences > Additional boards manager URLs**, add this URL, preserving any entries you already have:

   ```text
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

2. Open **Tools > Board > Boards Manager**, search for **esp32**, and install **esp32 by Espressif Systems**, version **3.3.0**. The sketch targets the 3.x API. No additional graphics, touch, or LVGL libraries are required.
3. Select **Tools > Board > esp32 > ESP32S3 Dev Module**.
4. Apply these settings:

   | Setting | Value |
   | --- | --- |
   | CPU Frequency | 240MHz (WiFi) |
   | Flash Size | 16MB (128Mb) |
   | PSRAM | **OPI PSRAM** |
   | USB CDC On Boot | **Enabled** |
   | USB Mode | Hardware CDC and JTAG |
   | Upload Mode | UART0 / Hardware CDC |
   | Flash Mode | QIO 80MHz |
   | Partition Scheme | Default; this small sketch does not need a custom partition |
   | Upload Speed | 460800; use 115200 if uploading is unreliable |

5. Connect the board with a **USB-C data cable**, choose its port under **Tools > Port**, and click **Verify**, then **Upload**.
6. Press **RST** after uploading if the animation does not start automatically. If the board is powered off, hold **PWR** for about 2-3 seconds.

Uploading replaces the currently installed application, including a factory voice-assistant demo if present.

If upload stays at `Connecting...`: hold **BOOT**, tap **RST**, release **BOOT**, select the port again, and upload. After success, tap **RST** without holding BOOT. Open **Serial Monitor at 115200 baud** for diagnostics.

## Hardware configuration

The PCB in the photo is marked **VIEWE-Round-V1.1 / 202506**. The requested 1.5-inch configuration is the manufacturer's **466 x 466 CO5300 AMOLED**.

There is a detail to be aware of: the ribbon in the supplied photo appears to occupy the connector marked 1.75, whereas the manufacturer shows its 1.5-inch panel using the other 24-pin connector. Both documented AMOLED sizes use CO5300, 466 x 466 resolution, and the same ESP32 bus pins. This sketch targets that shared hardware; it does not target the 1.85-inch TFT version. No ribbon changes are needed for this sketch.

| Function | ESP32-S3 GPIO |
| --- | --- |
| Display CS / CLK | 12 / 48 |
| Display D0 / D1 / D2 / D3 | 13 / 47 / 21 / 14 |
| Display reset | 11 |
| Touch SDA / SCL | 17 / 18 |
| Touch reset / interrupt | 10 / 9 |

The manufacturer's 1.5-inch sample uses CST816-style touch registers, while its specification lists CST9217. The sketch probes **0x5A** for CST9217 and **0x15** for CST816/820, and implements both protocols. Touch position does not need calibration because any contact triggers the effect.

The vendor sample declares a width of 471 despite the panel specification stating 466. This sketch uses **466 x 466**, with the vendor's initial column offset **6**. If your panel revision shows a horizontal shift, `LCD_X_OFFSET` at the top of the sketch is the adjustment point; the default is an informed configuration, not a hardware-tested calibration.

## What was checked

- Display bus pins were cross-checked between the manufacturer's example and schematic.
- Touch protocols were checked against the manufacturer's drivers and the CST92xx protocol documentation.
- Native drawing uses the original supplied path contours for the segmented and notched rings. Circular strokes are sampled as polygons. The supplied SVG contains references to missing masks; those shapes are drawn directly to preserve their intended outlines.
- An offline geometry check covered a complete 168-second combined animation cycle at five touch-transition positions. The artwork remained inside the circular screen.
- `preview.png` is an offline rendering of the sketch's geometry, showing idle and touch-held states. It is not a photograph of the device.
- **The firmware has not been uploaded or tested on your physical board. A full Arduino compile was not completed here:** the ESP32 board-package download failed twice with network timeouts. Run Verify after installing the board package. Existing Pico and AVR board installations were not changed.

## Troubleshooting

- **`Arduino.h` / ESP32 header missing or wrong-board error:** select ESP32S3 Dev Module after installing the Espressif package.
- **Serial says to enable PSRAM:** choose OPI PSRAM and upload again.
- **Animation works but touch does not:** inspect the Serial Monitor for the detected address. Hold BOOT to confirm that the visual transition works. If neither address is found, the sketch retries every two seconds; touch the screen while it retries.
- **Completely black screen:** confirm power, then inspect the Serial Monitor after pressing RST. Check that your actual panel is CO5300 AMOLED and not the alternate TFT model. The sketch cannot detect a wrong display controller through its write-only QSPI interface.
- **Brightness:** change `BRIGHTNESS` near the top (0-255; default 180).
- **Frame rate:** `FRAME_MS = 33` requests up to 30 fps. Actual speed depends on rendering and QSPI transfer time; it is not a measured performance claim.

## Sources and credits

- Artwork: **Nawsome, Uiverse.io**, HTML/SVG/CSS provided by the user.
- [VIEWE manufacturer repository](https://github.com/VIEWESMART/ESP32-S3-Round-Dev-Board)
- [Board specification V1.1](https://github.com/VIEWESMART/ESP32-S3-Round-Dev-Board/blob/main/datasheet/ESP32%20S3%20Round%20Dev%20Board%20SPEC%20V1.1.pdf)
- [Board schematic](https://github.com/VIEWESMART/ESP32-S3-Round-Dev-Board/blob/main/schematic/PCBA-VIEWE-Round-V1.1.sch.pdf)
- [Manufacturer's 1.5-inch initialization and pin assignments](https://github.com/VIEWESMART/ESP32-S3-Round-Dev-Board/blob/main/examples/esp-idf/ESP32-S3-Round-Dev-Board/viewe1.5/main/main.c)
- [Manufacturer's CST9217 touch implementation](https://github.com/VIEWESMART/ESP32-S3-Round-Dev-Board/blob/main/examples/esp-idf/ESP32-S3-Round-Dev-Board/viewe1.75/components/esp_lcd_touch_cst9217/esp_lcd_touch_cst9217.c)
- [CST92xx report-acknowledgement protocol](https://components.espressif.com/components/78/esp_lcd_touch_cst92xx/versions/0.1.0/readme)

Hardware initialization is adapted from the manufacturer's Apache-2.0 examples and the Espressif-authored panel driver (Copyright 2023 Espressif Systems (Shanghai) CO LTD). The implementation here replaces their LVGL application with a standalone Arduino renderer. The `research` folder contains reference material and offline checks; Arduino does not need it.
