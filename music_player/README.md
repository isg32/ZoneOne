# ZoneOne music player — merged display + DAC + SD sketch

`music_player.ino` merges two previously-separate demos
(`../PinConfigDemo/v2.ino.ino` for the display/DAC/buttons and
`../PinConfigDemo/sd_basic_test.ino` for the microSD card) into one
sketch, resolving the GPIO conflict between them, with playback reading
files directly off the SD card instead of streaming over WiFi.

## Hardware

- Edgehax ESP32-S3-WROOM-1 N16R8 dev board, built-in microSD slot
- 2.4" ST7789V SPI TFT display (240×320, 10-pin)
- CJMCU-1334 (UDA1334A) I2S DAC breakout
- 5 momentary push buttons: PLAY / NEXT / PREV / UP / DOWN

## Pin map

GPIO 10/11/12/13 are hardwired to the board's built-in microSD slot and
must never be reassigned. Everything else routes around them:

| Function | GPIO | Notes |
|---|---|---|
| SD CS | 10 | fixed by the board |
| SD SCK | 12 | fixed by the board |
| SD MOSI | 11 | fixed by the board |
| SD MISO | 13 | fixed by the board |
| Display SCK | 18 | moved off GPIO11 (SD MOSI conflict) |
| Display MOSI | 38 | moved off GPIO10 (SD CS conflict) |
| Display MISO | *(unused)* | dropped entirely — the ST7789 is write-only in this code, nothing ever reads from it |
| Display CS | 9 | |
| Display DC (RS) | 48 | moved off GPIO46, which is **input-only** on ESP32-S3 and can't drive a data/command line |
| Display RESET | 3 | |
| Display backlight | 47 | driven directly by code as a switch |
| I2S BCK | 15 | |
| I2S WS (LRCK) | 16 | |
| I2S DOUT (DIN) | 17 | |
| Button PLAY | 4 | `INPUT_PULLUP`, active low |
| Button NEXT | 5 | `INPUT_PULLUP`, active low |
| Button PREV | 6 | `INPUT_PULLUP`, active low |
| Button UP | 7 | `INPUT_PULLUP`, active low |
| Button DOWN | 8 | `INPUT_PULLUP`, active low |

The display gets its own independent SPI peripheral (ESP32-S3's second
general-purpose SPI host, on GPIO18/38) rather than sharing wires with
the SD card's bus — no CS-switching logic is needed between the two.

Avoided entirely: GPIO0/3/45/46 (strapping pins), GPIO19/20 (USB D+/D-),
GPIO43/44 (UART0, wired to this board's CH340 serial bridge),
GPIO40/41/42 (tied to the board's onboard status LEDs), and GPIO33-37
(potentially reserved for this module's Octal PSRAM).

**Known-flaky pin, not used here:** a prior bring-up of what looks like
this same physical board found GPIO14 already occupied by something
undocumented. It isn't needed by this pin map, but verify with a
multimeter before ever assigning it to anything.

## Wiring

**Display (2.4" ST7789V, 10-pin)**

| Display pin | Connects to |
|---|---|
| GND (pins 1 & 8) | GND |
| RS (Data/Command) | GPIO48 |
| CS | GPIO9 |
| SCL (clock) | GPIO18 |
| SDA (data in) | GPIO38 |
| RESET | GPIO3 |
| VDD | 3V3 |
| LED+ | GPIO47 |
| LED- | GND |

**DAC (CJMCU-1334 / UDA1334A)** — match by function, since exact
silkscreen labels can vary by breakout batch:

| DAC pin (typical label) | Connects to |
|---|---|
| VIN | 5V |
| GND | GND |
| BCK / BCLK | GPIO15 |
| WSEL / LRCK | GPIO16 |
| DIN / MOSI | GPIO17 |
| SCK (system/master clock) | leave unconnected — the UDA1334A derives its clock from BCLK internally |

**SD card** — nothing to wire; it's the board's built-in slot.

**Buttons** — one leg to the GPIO, other leg to GND.

## Arduino IDE board settings

- Board: **ESP32S3 Dev Module**
- USB CDC On Boot: **Disabled** — this board is flashed/monitored
  through a CH340 UART bridge (`/dev/ttyUSB0`), not native USB. Leaving
  this Enabled routes Serial to the unconnected native-USB peripheral
  and you'll get silence on the serial monitor.
- PSRAM: OPI PSRAM
- Flash: 16MB (128Mb)

## Libraries

- **ESP32-audioI2S** by schreibfaul1 (Library Manager) — decodes
  MP3/WAV/FLAC/AAC, including directly off the SD card via
  `connecttoFS()`. This is the only external dependency.
- `SD`, `FS`, `SPI` ship with the ESP32 Arduino core — nothing to install.

## Scope — what this does and doesn't do

Does:
- Mounts the SD card, scans the root folder for `.mp3`/`.wav`/`.flac`
  files, and lists them in the Songs screen.
- Plays the selected file through the UDA1334A via I2S.
- Full click-wheel-style navigation (menu, song list, now playing,
  volume popup, transport chrome) ported unchanged from the original
  display demo.

Explicitly not implemented yet (out of scope for this pass):
- No ID3/FLAC tag reading — track title is just the filename; artist,
  album, duration, and bitrate are all blank/zero.
- No album art (was only ever fetched over HTTP in the original demo).
- No WiFi/Bluetooth — pure local SD playback.
- Auto-advance-to-next-track on end-of-file is wired through
  `audio_eof_mp3()` only. WAV/FLAC tracks currently need NEXT pressed
  manually at the end of a file.

## If the display shows garbage/noise after wiring

A prior physical bring-up on ESP32-S3 + ST7789 hardware found the
40MHz SPI clock (`DISP_SPI_HZ` near the top of the sketch) too
aggressive for breadboard/dupont-wire prototyping — signal integrity on
long jumper wires breaks down at that speed. If you see visual noise
rather than a clean picture, try lowering it to 10MHz first before
suspecting a wiring or pin mistake.
