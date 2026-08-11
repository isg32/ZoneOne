# ZoneOne breakout board — design reference

This is the wiring spec for turning the ZoneOne music player from a
hand-wired breadboard build into a breakout/carrier board: a single PCB
that an ESP32-S3-WROOM-1 dev board plugs into, with header connectors
for the display, DAC, and buttons, so the rest of the build is
plug-in-and-go instead of point-to-point wiring.

Every pin and signal here is taken directly from the working firmware
(`../music_player.ino`) and `../README.md` — those two files are the
source of truth. If this document and the firmware ever disagree,
the firmware wins.

## Board concept

```
                 ┌───────────────────────────────┐
                 │        ESP32-S3-WROOM-1        │
                 │      dev board (plugs in)      │
                 └───────────────┬─────────────────┘
                                  │  J1 — two long header
                                  │  rows, socketed
   ┌──────────────────────────────┴───────────────────────────────┐
   │                                                                │
   │                     BREAKOUT / CARRIER BOARD                  │
   │                                                                │
   │   J2 Display (10p) ──────┐   J3 DAC (14p) ──────┐  J4 Buttons │
   │                          │                        │   (6p)     │
   └──────────┬───────────────┴────────────┬───────────┴─────┬─────┘
              │                             │                 │
      2.4" ST7789V TFT              CJMCU-1334 (UDA1334A)   5x momentary
      (10-pin ribbon/header)          DAC breakout           push buttons
```

The dev board is the one part that stays generic and swappable — it
sits in socket **J1**, which mirrors its own two header rows
(2.54 mm / 0.1" pitch, same as every other connector on this board).
Everything downstream — display, DAC, buttons — plugs into its own
dedicated header. Nobody has to hand-wire GPIOs to peripherals again;
they just plug three connectors into a board that's already wired
correctly.

**Before laying out J1's footprint**, measure the exact row spacing on
your specific ESP32-S3-WROOM-1 N16R8 dev board in hand — dev-board
clones vary in header pitch/spacing between vendors even when the
silkscreen GPIO labels match. Don't trust a generic pinout diagram for
the physical footprint; trust calipers on the real board.

## Unified pin map (single source of truth)

This is `../README.md`'s pin table, unchanged — repeated here so the
breakout documentation is self-contained.

| Function | ESP32-S3 GPIO | Goes to connector | Notes |
|---|---|---|---|
| SD CS | 10 | *(none — onboard slot)* | fixed by the dev board, not brought out to a breakout connector |
| SD SCK | 12 | *(none — onboard slot)* | fixed by the dev board |
| SD MOSI | 11 | *(none — onboard slot)* | fixed by the dev board |
| SD MISO | 13 | *(none — onboard slot)* | fixed by the dev board |
| Display SCK | 18 | J2 pin 4 (SCL) | |
| Display MOSI | 38 | J2 pin 5 (SDA) | |
| Display MISO | — | *(not wired)* | ST7789 is write-only here; nothing ever reads from it |
| Display CS | 9 | J2 pin 3 (CS) | |
| Display DC (RS) | 48 | J2 pin 2 (RS) | GPIO46 is input-only on ESP32-S3 and can't drive this line — don't substitute it |
| Display RESET | 3 | J2 pin 6 (RESET) | |
| Display backlight | 47 | J2 pin 9 (LED+) | driven directly by firmware as a switch, not PWM-dimmed |
| I2S BCK | 15 | J3 pin 6 (BCLK) | |
| I2S WS (LRCK) | 16 | J3 pin 4 (WSEL) | |
| I2S DOUT (DIN) | 17 | J3 pin 5 (DIN) | |
| Button PLAY | 4 | J4 pin 1 | `INPUT_PULLUP`, active low |
| Button NEXT | 5 | J4 pin 2 | `INPUT_PULLUP`, active low |
| Button PREV | 6 | J4 pin 3 | `INPUT_PULLUP`, active low |
| Button UP | 7 | J4 pin 4 | `INPUT_PULLUP`, active low |
| Button DOWN | 8 | J4 pin 5 | `INPUT_PULLUP`, active low |

Avoided entirely on this board (same list as `../README.md`): GPIO0/3\*/45/46
(strapping pins — GPIO3 is the one exception, reused for display RESET,
which is safe because RESET is only driven after boot), GPIO19/20
(native USB D+/D-), GPIO43/44 (UART0 / CH340 bridge), GPIO40/41/42
(onboard status LEDs), GPIO33-37 (reserved for Octal PSRAM on the
N16R8 module).

## Connector-by-connector wiring

### J2 — Display (2.4" ST7789V, 10-pin, single row)

Pin order matches the display module's own silkscreen exactly, so a
straight ribbon/header cable is a direct 1:1 plug — no crossed wires.

| J2 pin | Display signal | Goes to |
|---|---|---|
| 1 | GND | GND |
| 2 | RS (Data/Command) | GPIO48 |
| 3 | CS | GPIO9 |
| 4 | SCL (clock) | GPIO18 |
| 5 | SDA (data in) | GPIO38 |
| 6 | RESET | GPIO3 |
| 7 | VDD | 3V3 |
| 8 | GND | GND |
| 9 | LED+ | GPIO47 |
| 10 | LED- | GND |

### J3 — DAC (CJMCU-1334 / UDA1334A, 15-pin, single row)

Pin order matches this module's own edge-connector labeling: `VIN 3V0
GND WSEL DIN BCLK LOUT AGND ROUT SCK SF1 MUTE SF0 PLL DEEM`.

| J3 pin | DAC signal | Goes to | Notes |
|---|---|---|---|
| 1 | VIN | 5V | |
| 2 | 3V0 | *(not connected)* | this is the DAC module's own regulated 3.3V **output**, not an input — don't back-feed it |
| 3 | GND | GND | |
| 4 | WSEL (LRCK) | GPIO16 | |
| 5 | DIN | GPIO17 | |
| 6 | BCLK | GPIO15 | |
| 7 | LOUT | 3.5mm jack, left | |
| 8 | AGND | GND | tied to digital GND on this simple design — no separate analog ground plane |
| 9 | ROUT | 3.5mm jack, right | |
| 10 | SCK (master clock) | *(not connected)* | the UDA1334A derives its clock from BCLK internally; leaving this floating is correct, not an oversight |
| 11 | SF1 | **GND — hardwire on the PCB** | format select; LOW+LOW = standard Philips I2S, which matches the firmware and this DAC's default strap |
| 12 | MUTE | **GND — hardwire on the PCB** | active **HIGH** on the real UDA1334A datasheet — tying to GND keeps it permanently unmuted |
| 13 | SF0 | **GND — hardwire on the PCB** | see SF1 |
| 14 | PLL | **GND — hardwire on the PCB** | selects internal-PLL/Audio mode |
| 15 | DEEM | **GND — hardwire on the PCB** | de-emphasis; GND = off |

**This is the one deliberate improvement over the breadboard build.**
During bring-up, SF0/SF1/MUTE/PLL/DEEM were loose jumper wires to GND,
and getting MUTE's polarity backwards (or leaving a strap floating on
this particular clone board, which lacks onboard pull-downs) was the
exact cause of the garbled/muted audio that took several iterations to
track down. On the breakout PCB, hardwire all five of those pins to
the GND plane directly — don't expose them as header pins at all. That
removes an entire category of assembly mistakes for anyone building
this from the breakout board instead of from scratch.

### J4 — Buttons (6-pin, single row: 5 signals + shared ground)

| J4 pin | Function | Goes to |
|---|---|---|
| 1 | PLAY | GPIO4 |
| 2 | NEXT | GPIO5 |
| 3 | PREV | GPIO6 |
| 4 | UP | GPIO7 |
| 5 | DOWN | GPIO8 |
| 6 | GND (shared return) | GND |

Each button is a simple momentary switch: one leg to its numbered
signal pin, the other leg to the shared GND pin. The firmware enables
internal pull-ups (`INPUT_PULLUP`) on all five GPIOs, so no external
pull-up/pull-down resistors are needed on the board — a button press
just needs to short its signal pin to GND.

## Power

The breakout board takes **all** of its power from the dev board
plugged into J1 — there's no separate power input connector.

- **5V** (J1, sourced from the dev board's own USB-C or battery input)
  feeds J3 pin 1 (DAC VIN) directly.
- **3V3** (J1, the dev board's own regulated output) feeds J2 pin 7
  (display VDD).
- A single GND plane ties J1, J2, J3, and J4 grounds together, along
  with the DAC's AGND (see J3 note above).

No level shifting is needed anywhere: the display and DAC's logic
inputs are driven directly by the ESP32-S3's 3.3V GPIOs, matching both
modules' expected logic levels.

## Bill of materials

| Part | Qty | Notes |
|---|---|---|
| ESP32-S3-WROOM-1 N16R8 dev board | 1 | plugs into J1; not part of the breakout PCB itself |
| 2.4" ST7789V SPI TFT, 10-pin | 1 | |
| CJMCU-1334 (UDA1334A) I2S DAC breakout | 1 | |
| Momentary push button | 5 | PLAY / NEXT / PREV / UP / DOWN |
| 2.54mm (0.1") female header, 2×N row matching J1 | 1 set | measure your specific dev board — see note above |
| 2.54mm female header, 10-pin single row | 1 | J2, display |
| 2.54mm female header, 15-pin single row | 1 | J3, DAC |
| 2.54mm female header, 6-pin single row | 1 | J4, buttons |
| 3.5mm audio jack | 1 | wired to J3 LOUT/ROUT/AGND |

Use female headers for J1-J4 throughout, so every peripheral (which
already ships with male pins) plugs straight in without extra cabling.

## Firmware and board settings

Not repeated here — see `../README.md` for the Arduino IDE board
settings (PSRAM, flash size, USB CDC On Boot), the library list, and
the `DISP_SPI_HZ` note about signal integrity at 40MHz on long wiring
(less of a concern on a proper PCB than on dupont jumpers, but worth
knowing if display noise ever reappears).

## Diagrams

See `diagrams/`:

- `system-overview.svg` — how the four boards connect, at a glance.
- `connector-pinouts.svg` — J2/J3/J4 pin-by-pin, matching the tables
  above.
