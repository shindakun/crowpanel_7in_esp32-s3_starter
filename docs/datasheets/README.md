# Reference schematics

Hardware reference material for the CrowPanel Advance 7" board, copied from the
[Elecrow CrowPanel GitHub](https://github.com/Elecrow-RD/CrowPanel-Advance-7-HMI-ESP32-S3-AI-Powered-IPS-Touch-Screen-800x480).
These schematics are the authoritative source for pin maps and RGB timings used
by the `crowpanel` and `audio` components.

| File | What it is |
| --- | --- |
| `CrowPanel-Advance-7-schematic-V1.4.pdf` | Full V1.4 board schematic. This is the revision this template targets and the one to trust for pin assignments. |
| `CrowPanel-Advance-7-schematic-V1.3.pdf` | V1.3 board schematic, for reference on older units. |

The mic fitted on the V1.4 board is a **PDM microphone (LMD3526B261)** on
CLK=IO19, DATA=IO20 (no WS line), per the V1.4 schematic. Elecrow's upstream
repo also ships an INMP441 datasheet, but that is not the fitted part and is not
included here because it describes a different mic on different pins.

## STC8H1K28 command bytes

The V1.4 schematic shows the wiring (backlight, touch enable, speaker amp, and
the passive buzzer on `P2_7/BEEP`) but not the firmware command protocol. The
command bytes (0/245 backlight, 250 touch, 248 speaker amp, 246/247 buzzer) come
from the Elecrow example code and README on the GitHub repo above.
