# CrowPanel Advance 7.0 (ESP32-S3) starter

ESP-IDF starter template for the **Elecrow CrowPanel Advance 7.0-HMI** 7-inch
ESP32-S3 touch display, also sold rebranded (e.g. IoTeikXgo, Amazon B0GVDBN9RT).

Clone this directory to begin a new project. Board support lives in a reusable
`crowpanel` component; replace the body of `main/main.c` with your own code.

## Hardware

| | |
|---|---|
| MCU | ESP32-S3-WROOM-1 N16R8 (16MB flash, 8MB octal PSRAM) |
| Display | 7" IPS 800x480, SC7277 RGB driver, 16-bit RGB565 |
| Touch | GT911 capacitive, I2C addr 0x5D |
| Backlight + touch enable | STC8H1K28 control MCU, I2C addr 0x30 (board V1.3+) |
| USB-serial | CH340 |

This template targets the **V1.3+** board revision, where an STC8H1K28 MCU at
I2C 0x30 controls the backlight (single command byte: 0 = full, 245 = off) and
touch enable. Older V1.0/1.2 boards instead use a CH422G expander at I2C
0x24/0x38 (write 0x01 to 0x24, then 0x1E to 0x38 to turn the backlight on); if
your screen stays black, you likely have an older revision and need to adjust
`components/crowpanel/crowpanel.c` accordingly.

## Build and flash

```sh
. ~/esp/esp-idf/export.sh        # or wherever your IDF lives (built against v5.5.2)
idf.py set-target esp32s3        # only needed once
idf.py build
idf.py -p /dev/cu.wchusbserial14210 flash monitor
```

### macOS: CH340 driver

This board's USB bridge is a CH340. macOS does not auto-bind it; install the
WCH driver, then the port appears as `/dev/cu.wchusbserial*`:

```sh
brew install --cask wch-ch34x-usb-serial-driver
# then approve the extension in System Settings > General >
# Login Items & Extensions > Driver Extensions, and reboot.
```

If the board does not enumerate at all: it is almost always a charge-only USB-C
cable. Confirm the cable carries data (a phone should mount on it).

## Using the board API

```c
#include "crowpanel.h"

void app_main(void) {
    crowpanel_init();                              // panel + touch + backlight on

    crowpanel_fill(CROWPANEL_RGB565(0, 0, 255));   // solid blue
    crowpanel_set_brightness(50);                  // 0..100 %

    crowpanel_touch_point_t p;
    while (1) {
        if (crowpanel_get_touch(&p)) {
            printf("touch %u,%u\n", p.x, p.y);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
```

For LVGL or custom rendering, `crowpanel_panel_handle()` and
`crowpanel_touch_handle()` expose the underlying esp_lcd handles.

## What the default demo does

`main/main.c` is a self-test: it cycles the screen through solid colors, ramps
the backlight brightness up and down, logs GT911 touch coordinates to the
serial console, and dims the backlight while the screen is touched. Flashing a
fresh clone proves display, touch, and backlight all work before you write a
line of your own code.

## Project layout

```
.
├── CMakeLists.txt
├── sdkconfig.defaults          # N16R8: octal PSRAM, 16MB flash, 240MHz
├── main/
│   ├── CMakeLists.txt
│   └── main.c                  # demo / your app
└── components/
    └── crowpanel/              # reusable board support
        ├── CMakeLists.txt
        ├── idf_component.yml   # pulls esp_lcd_touch_gt911
        ├── include/crowpanel.h
        └── crowpanel.c
```
