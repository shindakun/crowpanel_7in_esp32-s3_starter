# CrowPanel Advance 7" (ESP32-S3) starter

ESP-IDF starter template for the **Elecrow CrowPanel Advance 7.0-HMI** 7-inch
ESP32-S3 touch display, also sold rebranded (e.g. IoTeikXgo, Amazon B0GVDBN9RT).

Clone this directory to begin a new project. Board support lives in a reusable
`crowpanel` component; replace the body of `main/main.c` with your own code.

## Hardware

| Part | Detail |
| --- | --- |
| MCU | ESP32-S3-WROOM-1 N16R8 (16MB flash, 8MB octal PSRAM) |
| Display | 7" IPS 800x480, SC7277 RGB driver, 16-bit RGB565 |
| Touch | GT911 capacitive, I2C addr 0x5D |
| Backlight + touch | STC8H1K28 control MCU, I2C addr 0x30 (board V1.3+) |
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
idf.py -p PORT flash monitor     # PORT e.g. /dev/cu.wchusbserial* (macOS) or /dev/ttyUSB0
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

## What the base provides

| Component | Purpose |
| --- | --- |
| `crowpanel` | RGB panel, GT911 touch, backlight/brightness (STC8H1K28) |
| `crowpanel_lvgl` | LVGL 9 bound to the panel + touch via esp_lvgl_port |
| `net` | NVS init, Wi-Fi STA connect/disconnect, auto-reconnect, IP/status, SNTP |
| `sdcard` | FAT-over-SPI microSD mount (`/sdcard`) |
| `audio` | INMP441 I2S mic capture + I2S speaker/amp output |

> **Not yet hardware-verified:** `crowpanel` (display, touch, backlight) and
> `net` are confirmed working on a physical board, including Wi-Fi connect, IP
> readout, auto-reconnect, and SNTP time sync. `sdcard` and `audio` are **not**
> fully tested on hardware yet: they compile and initialize, but a real SD mount
> and real speaker/mic audio have not been confirmed (verifying them needs the
> tiny S0/S1 DIP switches set correctly, see below). Pin maps come from
> Elecrow's docs/code but treat these two as provisional until verified.

The microSD slot and the I2S speaker share GPIO 4/5/6 via the on-board S0/S1
DIP switches, so they cannot be used at the same time:

- **TF Card position** (V1.3+: S1=1, S0=1; V1.0/1.2: S1=1, S0=0): `sdcard_mount()`
  works; the speaker is disconnected.
- **MIC & SPK position** (S1=0, S0=0): the speaker works; SD is disconnected.

In the wrong position the corresponding init returns an error. The INMP441
microphone is on independent pins (BCLK=19, WS=2, DATA=20) and works in either
position.

## Using the board API

Raw drawing + touch + brightness:

```c
#include "crowpanel.h"

crowpanel_init();                              // panel + touch + backlight on
crowpanel_fill(CROWPANEL_RGB565(0, 0, 255));   // solid blue
crowpanel_set_brightness(50);                  // 0..100 %

crowpanel_touch_point_t p;
if (crowpanel_get_touch(&p)) { /* p.x, p.y */ }
```

LVGL UI (most projects):

```c
#include "crowpanel.h"
#include "crowpanel_lvgl.h"

crowpanel_init();
crowpanel_lvgl_init();
if (crowpanel_lvgl_lock(0)) {                   // LVGL runs in its own task
    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Hello");
    crowpanel_lvgl_unlock();
}
```

Wi-Fi (auto-reconnects on drops after the first connect):

```c
#include "net.h"

if (net_wifi_connect("my-ssid", "my-pass", 15000) == ESP_OK) {  // blocks until IP
    char ip[16];
    net_wifi_get_ip(ip, sizeof(ip));   // e.g. "10.0.0.110"
    net_sntp_sync(10000);              // set the clock from NTP
}
bool up = net_wifi_is_connected();
net_wifi_disconnect();                 // stops auto-reconnect
```

SD card (requires the S0/S1 switches in the TF Card position, see above):

```c
#include "sdcard.h"

if (sdcard_mount() == ESP_OK) {                  // mounts FAT at /sdcard
    FILE *f = fopen("/sdcard/log.txt", "w");
    fprintf(f, "hi");
    fclose(f);
}
```

Audio (mic always works; speaker needs the MIC & SPK switch position):

```c
#include "audio.h"

audio_mic_init(16000);                           // INMP441 capture
int16_t buf[256]; size_t got;
audio_mic_read(buf, sizeof(buf), &got);

audio_speaker_init(16000);                        // I2S out + amp on
audio_speaker_play_tone(440, 500);                // 440 Hz for 500 ms
```

For custom rendering, `crowpanel_panel_handle()` and `crowpanel_touch_handle()`
expose the underlying esp_lcd handles.

## What the default demo does

`main/main.c` builds a small LVGL UI: a title label, a "Touches: N" counter, a
"Tap me" button that increments the counter, a "Brightness" slider that dims
the backlight live, an "SD: ..." status line from mounting the microSD and
writing a test file, a "Wi-Fi: ..." status line from connecting to a network,
and a live "Mic level" bar from the INMP441. (The speaker is not driven by the
demo because it shares pins with SD; see the note above.)

The Wi-Fi test reads `WIFI_SSID` / `WIFI_PASS` set near the top of `main.c`;
leave `WIFI_SSID` empty (the default) to skip it. Do not commit real
credentials, blank them before committing.

Flashing a fresh clone exercises display, touch, backlight, Wi-Fi, SD, and mic.
Wi-Fi (the `net` component: connect, IP, auto-reconnect, SNTP) is
hardware-verified; the SD and mic/audio paths are not yet hardware-verified.

## Partitions

`partitions.csv` (16MB flash) is OTA-ready: two 4MB app slots (`ota_0`,
`ota_1`), an `otadata` partition, `nvs`, and a 6MB `storage` (SPIFFS)
partition. The demo runs from `ota_0`; **`ota_1` is reserved for future OTA
updates** and is unused until you add OTA code. If you will never OTA, you can
replace the two `ota_*` app entries with a single `factory` app to reclaim 4MB.

## Contributing

CI (`.github/workflows/ci.yml`) builds the firmware with ESP-IDF and runs
clang-format, markdownlint, and whitespace checks on every push and PR.

Run the same checks locally before committing:

```sh
pip install pre-commit
pre-commit install          # enable the git hook
pre-commit run --all-files  # run on the whole tree
```

C is formatted with `.clang-format`; Markdown follows `.markdownlint.json`.

A local `readme-sync` hook blocks a commit that changes `components/` or
`main/` without also updating `README.md`, so the docs do not drift behind the
code. If a change genuinely needs no doc update, commit with
`SKIP_README_SYNC=1 git commit ...` or `git commit --no-verify`.

## Project layout

```text
.
├── CMakeLists.txt
├── LICENSE                     # Apache-2.0
├── NOTICE
├── partitions.csv              # OTA (2x4MB app) + 6MB spiffs + nvs
├── sdkconfig.defaults          # N16R8: octal PSRAM, 16MB flash, 240MHz, LVGL fonts
├── .clang-format               # C style (enforced by CI + pre-commit)
├── .markdownlint.json          # doc lint rules
├── .markdownlintignore
├── .pre-commit-config.yaml     # local hooks: clang-format, markdownlint, whitespace
├── .github/workflows/ci.yml    # build firmware + lint on push/PR
├── scripts/check-readme-sync.sh
├── main/
│   ├── CMakeLists.txt
│   └── main.c                  # demo / your app
└── components/
    ├── crowpanel/              # display, touch, backlight/brightness
    │   ├── CMakeLists.txt
    │   ├── idf_component.yml   # pulls esp_lcd_touch_gt911, esp_lvgl_port, lvgl
    │   ├── include/crowpanel.h
    │   ├── include/crowpanel_lvgl.h
    │   ├── crowpanel.c
    │   └── crowpanel_lvgl.c    # LVGL 9 integration
    ├── net/                    # NVS + Wi-Fi station connect
    │   ├── CMakeLists.txt
    │   ├── include/net.h
    │   └── net.c
    ├── sdcard/                 # FAT-over-SPI microSD mount
    │   ├── CMakeLists.txt
    │   ├── include/sdcard.h
    │   └── sdcard.c
    └── audio/                  # INMP441 mic + I2S speaker/amp
        ├── CMakeLists.txt
        ├── include/audio.h
        └── audio.c
```

## License

Apache-2.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE).
