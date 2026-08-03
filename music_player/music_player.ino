/*
 * ============================================================
 *  ESP32-S3 iPod  —  Music Player  (merged display+DAC+SD build)
 * ============================================================
 *
 *  NAVIGATION MODEL (clear and consistent):
 *  ┌─────────────────────────────────────────────────────────┐
 *  │  BUTTON    SHORT PRESS         LONG PRESS (600ms)       │
 *  ├─────────────────────────────────────────────────────────┤
 *  │  UP        Scroll up           Volume up (now playing)  │
 *  │  DOWN      Scroll down         Back one screen          │
 *  │  PLAY      Select / Play/Pause  —                       │
 *  │  NEXT      Next track          Fast forward +10s        │
 *  │  PREV      Prev track / Back   Rewind -10s              │
 *  └─────────────────────────────────────────────────────────┘
 *
 *  SCREEN STACK:
 *    MENU → SONGS LIST → NOW PLAYING
 *    Long DOWN or Long PREV = go back one level
 *
 *  Volume is shown as a popup overlay on UP/DOWN in Now Playing.
 *
 *  Required libraries (Library Manager):
 *    • ESP32-audioI2S  by schreibfaul1  (decodes MP3/WAV/FLAC/AAC,
 *      including directly off the SD card via connecttoFS)
 *  SD/FS/SPI ship with the ESP32 Arduino core, no install needed.
 *
 *  Board settings:
 *    Board   : ESP32S3 Dev Module
 *    USB Mode: USB-CDC On Boot → Disabled (this board is a CH340 UART
 *              bridge on /dev/ttyUSB0, not native USB — matches
 *              sd_basic_test.ino's confirmed-working config)
 *    PSRAM   : OPI PSRAM
 *    Flash   : 16MB (128Mb)
 *
 *  Battery: Connect LiPo directly to BATT pins on board.
 *           Onboard IC charges via USB-C automatically.
 * ============================================================
 */

// ============================================================
//  PINS
//
//  GPIO 10/11/12/13 belong to the board's built-in microSD slot
//  (hardwired on the PCB, not reassignable) — the display's SPI bus
//  below is on its own separate pins so it doesn't collide with them.
// ============================================================
#define PIN_MOSI        38   // display SPI — was 10, collided with SD CS
#define PIN_CLK         18   // display SPI — was 11, collided with SD MOSI
// No PIN_MISO: the ST7789 is write-only here, nothing ever reads from
// it (was 12, which collided with SD SCK).
#define PIN_DISP_CS      9
#define PIN_DISP_DC     48   // was 46, which is input-only on ESP32-S3
#define PIN_DISP_RST     3
#define PIN_DISP_BL     47
#define PIN_I2S_BCK     15
#define PIN_I2S_WS      16
#define PIN_I2S_DOUT    17
#define PIN_BTN_PLAY     4
#define PIN_BTN_NEXT     5
#define PIN_BTN_PREV     6
#define PIN_BTN_UP       7
#define PIN_BTN_DOWN     8

// ============================================================
//  INCLUDES
// ============================================================
#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <Audio.h>
// ============================================================
//  DISPLAY
// ============================================================
#define DISP_W       240
#define DISP_H       320
#define DISP_SPI_HZ  40000000UL

#define C_BLACK      0x0000
#define C_WHITE      0xFFFF
#define C_LTGRAY     0xDEDB
#define C_MIDGRAY    0xC618
#define C_DKGRAY     0x8C51
#define C_NAVY       0x000D
#define C_RED        0xF800
#define C_GREEN      0x07E0
#define C_YELLOW     0xFFE0
#define C_ROWSEL     0x0000
#define C_ROWBG      0xFFFF
#define C_ROWALT     0xEF7D
#define C_TBAR       0x0000
#define C_WINBDR     0x6B4D
#define C_VOLBG      0x2104   // volume popup background

// ============================================================
//  LAYOUT
// ============================================================
#define TITLEBAR_H   22
#define WIN_Y        22
#define WIN_HDR_H    20
#define CONTENT_Y    42
#define STATUSBAR_H  20
#define TRANSPORT_H  50
#define TRANSPORT_Y  (DISP_H - TRANSPORT_H)
#define STATUSBAR_Y  (TRANSPORT_Y - STATUSBAR_H)
#define CONTENT_H    (STATUSBAR_Y - CONTENT_Y)

#define ROW_H        38
#define ROW_TEXT_X   12
#define ARROW_X      (DISP_W - 14)
#define VISIBLE_ROWS (CONTENT_H / ROW_H)

// Now playing layout
#define ART_X        28
#define ART_Y        (CONTENT_Y + 6)
#define ART_W        184
#define ART_H        176
#define TRACK_Y      (ART_Y + ART_H + 8)
#define ARTIST_Y     (TRACK_Y + 14)
#define PROG_Y       (ARTIST_Y + 20)
#define PROG_X       12
#define PROG_W       (DISP_W - 24)
#define PROG_H       6
#define TIME_Y       (PROG_Y + PROG_H + 4)

// ============================================================
//  FONT  5×7 sequential ASCII 0-127
// ============================================================
static const uint8_t FONT[128][5] = {
    {0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},
    {0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},
    {0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},
    {0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},
    {0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},
    {0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},
    {0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},
    {0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0},
    {0x00,0x00,0x00,0x00,0x00}, // 20 space
    {0x00,0x00,0x5F,0x00,0x00}, // 21 !
    {0x00,0x07,0x00,0x07,0x00}, // 22 "
    {0x14,0x7F,0x14,0x7F,0x14}, // 23 #
    {0x24,0x2A,0x7F,0x2A,0x12}, // 24 $
    {0x23,0x13,0x08,0x64,0x62}, // 25 %
    {0x36,0x49,0x55,0x22,0x50}, // 26 &
    {0x00,0x05,0x03,0x00,0x00}, // 27 '
    {0x00,0x1C,0x22,0x41,0x00}, // 28 (
    {0x00,0x41,0x22,0x1C,0x00}, // 29 )
    {0x08,0x2A,0x1C,0x2A,0x08}, // 2A *
    {0x08,0x08,0x3E,0x08,0x08}, // 2B +
    {0x00,0x50,0x30,0x00,0x00}, // 2C ,
    {0x08,0x08,0x08,0x08,0x08}, // 2D -
    {0x00,0x60,0x60,0x00,0x00}, // 2E .
    {0x20,0x10,0x08,0x04,0x02}, // 2F /
    {0x3E,0x51,0x49,0x45,0x3E}, // 30 0
    {0x00,0x42,0x7F,0x40,0x00}, // 31 1
    {0x42,0x61,0x51,0x49,0x46}, // 32 2
    {0x21,0x41,0x45,0x4B,0x31}, // 33 3
    {0x18,0x14,0x12,0x7F,0x10}, // 34 4
    {0x27,0x45,0x45,0x45,0x39}, // 35 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 36 6
    {0x01,0x71,0x09,0x05,0x03}, // 37 7
    {0x36,0x49,0x49,0x49,0x36}, // 38 8
    {0x06,0x49,0x49,0x29,0x1E}, // 39 9
    {0x00,0x36,0x36,0x00,0x00}, // 3A :
    {0x00,0x56,0x36,0x00,0x00}, // 3B ;
    {0x08,0x14,0x22,0x41,0x00}, // 3C <
    {0x14,0x14,0x14,0x14,0x14}, // 3D =
    {0x00,0x41,0x22,0x14,0x08}, // 3E >
    {0x02,0x01,0x51,0x09,0x06}, // 3F ?
    {0x32,0x49,0x79,0x41,0x3E}, // 40 @
    {0x7E,0x11,0x11,0x11,0x7E}, // 41 A
    {0x7F,0x49,0x49,0x49,0x36}, // 42 B
    {0x3E,0x41,0x41,0x41,0x22}, // 43 C
    {0x7F,0x41,0x41,0x22,0x1C}, // 44 D
    {0x7F,0x49,0x49,0x49,0x41}, // 45 E
    {0x7F,0x09,0x09,0x09,0x01}, // 46 F
    {0x3E,0x41,0x49,0x49,0x7A}, // 47 G
    {0x7F,0x08,0x08,0x08,0x7F}, // 48 H
    {0x00,0x41,0x7F,0x41,0x00}, // 49 I
    {0x20,0x40,0x41,0x3F,0x01}, // 4A J
    {0x7F,0x08,0x14,0x22,0x41}, // 4B K
    {0x7F,0x40,0x40,0x40,0x40}, // 4C L
    {0x7F,0x02,0x0C,0x02,0x7F}, // 4D M
    {0x7F,0x04,0x08,0x10,0x7F}, // 4E N
    {0x3E,0x41,0x41,0x41,0x3E}, // 4F O
    {0x7F,0x09,0x09,0x09,0x06}, // 50 P
    {0x3E,0x41,0x51,0x21,0x5E}, // 51 Q
    {0x7F,0x09,0x19,0x29,0x46}, // 52 R
    {0x46,0x49,0x49,0x49,0x31}, // 53 S
    {0x01,0x01,0x7F,0x01,0x01}, // 54 T
    {0x3F,0x40,0x40,0x40,0x3F}, // 55 U
    {0x1F,0x20,0x40,0x20,0x1F}, // 56 V
    {0x3F,0x40,0x38,0x40,0x3F}, // 57 W
    {0x63,0x14,0x08,0x14,0x63}, // 58 X
    {0x07,0x08,0x70,0x08,0x07}, // 59 Y
    {0x61,0x51,0x49,0x45,0x43}, // 5A Z
    {0x00,0x7F,0x41,0x41,0x00}, // 5B [
    {0x02,0x04,0x08,0x10,0x20}, // 5C backslash
    {0x00,0x41,0x41,0x7F,0x00}, // 5D ]
    {0x04,0x02,0x01,0x02,0x04}, // 5E ^
    {0x40,0x40,0x40,0x40,0x40}, // 5F _
    {0x00,0x01,0x02,0x04,0x00}, // 60 `
    {0x20,0x54,0x54,0x54,0x78}, // 61 a
    {0x7F,0x48,0x44,0x44,0x38}, // 62 b
    {0x38,0x44,0x44,0x44,0x20}, // 63 c
    {0x38,0x44,0x44,0x48,0x7F}, // 64 d
    {0x38,0x54,0x54,0x54,0x18}, // 65 e
    {0x08,0x7E,0x09,0x01,0x02}, // 66 f
    {0x08,0x14,0x54,0x54,0x3C}, // 67 g
    {0x7F,0x08,0x04,0x04,0x78}, // 68 h
    {0x00,0x44,0x7D,0x40,0x00}, // 69 i
    {0x20,0x40,0x44,0x3D,0x00}, // 6A j
    {0x7F,0x10,0x28,0x44,0x00}, // 6B k
    {0x00,0x41,0x7F,0x40,0x00}, // 6C l
    {0x7C,0x04,0x18,0x04,0x78}, // 6D m
    {0x7C,0x08,0x04,0x04,0x78}, // 6E n
    {0x38,0x44,0x44,0x44,0x38}, // 6F o
    {0x7C,0x14,0x14,0x14,0x08}, // 70 p
    {0x08,0x14,0x14,0x18,0x7C}, // 71 q
    {0x7C,0x08,0x04,0x04,0x08}, // 72 r
    {0x48,0x54,0x54,0x54,0x20}, // 73 s
    {0x04,0x3F,0x44,0x40,0x20}, // 74 t
    {0x3C,0x40,0x40,0x20,0x7C}, // 75 u
    {0x1C,0x20,0x40,0x20,0x1C}, // 76 v
    {0x3C,0x40,0x30,0x40,0x3C}, // 77 w
    {0x44,0x28,0x10,0x28,0x44}, // 78 x
    {0x0C,0x50,0x50,0x50,0x3C}, // 79 y
    {0x44,0x64,0x54,0x4C,0x44}, // 7A z
    {0x00,0x08,0x36,0x41,0x00}, // 7B {
    {0x00,0x00,0x7F,0x00,0x00}, // 7C |
    {0x00,0x41,0x36,0x08,0x00}, // 7D }
    {0x08,0x04,0x08,0x10,0x08}, // 7E ~
    {0x00,0x00,0x00,0x00,0x00}, // 7F
};

// ============================================================
//  APP STATE
// ============================================================

// Screen stack — supports going back
enum Screen { SCR_MENU, SCR_SONGS, SCR_NOWPLAYING };

struct Song {
    char title[64];   // filename only, for display
    char path[160];   // full path from SD root, for playback
    char artist[48];
    char album[48];
    int  duration_s;
    int  bitrate_kbps;
};

static Screen   g_screen       = SCR_MENU;
static Screen   g_prev_screen  = SCR_MENU;  // for back navigation

static int      g_menu_sel     = 0;
static int      g_song_sel     = 0;
static int      g_song_scroll  = 0;
static int      g_now_idx      = -1;
static bool     g_playing      = false;
static bool     g_paused       = false;
static uint32_t g_play_start   = 0;
static uint32_t g_paused_accum = 0;  // total ms spent paused
static uint32_t g_pause_began  = 0;  // when current pause started

static int      g_volume       = 80;  // 0–100 in firmware; maps to 0–21 for lib
static int      g_song_count   = 0;
static Song     g_songs[100];
static bool     g_fetched      = false;
static char     g_clock[6]     = "12:00";

// Volume popup
static bool     g_vol_visible  = false;
static uint32_t g_vol_hide_ms  = 0;   // hide popup after this millis()

// ============================================================
//  SPI / DISPLAY GLOBALS
// ============================================================
static SPIClass *g_spi = nullptr;
static Audio     g_audio;

// ============================================================
//  DISPLAY PRIMITIVES
// ============================================================
static void dsel()  { digitalWrite(PIN_DISP_CS, LOW);  }
static void ddes()  { digitalWrite(PIN_DISP_CS, HIGH); }
static void dcmd()  { digitalWrite(PIN_DISP_DC, LOW);  }
static void ddata() { digitalWrite(PIN_DISP_DC, HIGH); }

// ============================================================
//  FUNCTION PROTOTYPES (Add these here to fix the error)
// ============================================================
static void draw_nowplaying_content(bool full); // Added 'bool full'
static void draw_nowplaying_progress();
static void draw_volume_popup();

static void disp_cmd(uint8_t c)
{
    dsel(); dcmd(); g_spi->transfer(c); ddes();
}

static void disp_cmd_data(uint8_t c, const uint8_t *d, size_t n)
{
    dsel(); dcmd(); g_spi->transfer(c);
    if (d && n) { ddata(); g_spi->writeBytes(d, n); }
    ddes();
}

static void disp_set_win(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t ca[4] = {(uint8_t)(x0>>8),(uint8_t)(x0),(uint8_t)(x1>>8),(uint8_t)(x1)};
    uint8_t ra[4] = {(uint8_t)(y0>>8),(uint8_t)(y0),(uint8_t)(y1>>8),(uint8_t)(y1)};
    disp_cmd_data(0x2A, ca, 4);
    disp_cmd_data(0x2B, ra, 4);
    disp_cmd(0x2C);
}

static void disp_fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t col)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > DISP_W) w = DISP_W - x;
    if (y + h > DISP_H) h = DISP_H - y;

    disp_set_win(x, y, x + w - 1, y + h - 1);
    dsel(); 
    ddata();
    
    // Send pixels using write16 (standard Big-Endian for ST7789)
    // This removes the "Green/Purple" artifacts
    for (int32_t i = 0; i < (int32_t)w * h; i++) {
        g_spi->write16(col); 
    }
    ddes();
}

static void disp_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t col)
{
    disp_fill(x,       y,       w, 1, col);
    disp_fill(x,       y+h-1,   w, 1, col);
    disp_fill(x,       y,       1, h, col);
    disp_fill(x+w-1,   y,       1, h, col);
}

static void disp_char(int16_t x, int16_t y, char c, uint16_t fg, uint16_t bg, uint8_t sc)
{
    uint8_t idx = (uint8_t)c;
    if (idx >= 128) idx = 32;
    const uint8_t *g = FONT[idx];

    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        disp_set_win(x + col * sc, y, x + col * sc + sc - 1, y + 7 * sc - 1);
        dsel(); ddata();
        for (int row = 0; row < 7; row++) {
            uint16_t px = (bits & (1 << row)) ? fg : bg;
            for (int s = 0; s < sc; s++) {
                g_spi->write16(px);
            }
        }
        ddes();
    }
    disp_fill(x + 5 * sc, y, sc, 7 * sc, bg);
}

static void disp_str(int16_t x, int16_t y, const char *s,
                      uint16_t fg, uint16_t bg, uint8_t sc = 1)
{
    while (*s) { disp_char(x, y, *s, fg, bg, sc); x += 6*sc; s++; }
}

// Clip string to max_px pixels wide
static void disp_str_clip(int16_t x, int16_t y, const char *s,
                            uint16_t fg, uint16_t bg,
                            int16_t max_px, uint8_t sc = 1)
{
    int16_t cx = x;
    while (*s && (cx - x) + 6*sc <= max_px) {
        disp_char(cx, y, *s, fg, bg, sc);
        cx += 6*sc; s++;
    }
}

static void disp_num(int16_t x, int16_t y, uint32_t n,
                      uint16_t fg, uint16_t bg, uint8_t sc = 1)
{
    char buf[12];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)n);
    disp_str(x, y, buf, fg, bg, sc);
}

static void fmt_time(char *buf, size_t sz, int secs)
{
    if (secs < 0) secs = 0;
    snprintf(buf, sz, "%d:%02d", secs / 60, secs % 60);
}

// ============================================================
//  DISPLAY INIT
// ============================================================
static void init_display()
{
    pinMode(PIN_DISP_CS,  OUTPUT); digitalWrite(PIN_DISP_CS,  HIGH);
    pinMode(PIN_DISP_DC,  OUTPUT); digitalWrite(PIN_DISP_DC,  HIGH);
    pinMode(PIN_DISP_RST, OUTPUT); digitalWrite(PIN_DISP_RST, HIGH);
    pinMode(PIN_DISP_BL,  OUTPUT); digitalWrite(PIN_DISP_BL,  LOW);

    g_spi = new SPIClass(HSPI);
    g_spi->begin(PIN_CLK, -1, PIN_MOSI, -1);
    g_spi->beginTransaction(SPISettings(DISP_SPI_HZ, MSBFIRST, SPI_MODE0));

    digitalWrite(PIN_DISP_RST, LOW);  delay(20);
    digitalWrite(PIN_DISP_RST, HIGH); delay(150);
    disp_cmd(0x01); delay(150);
    disp_cmd(0x11); delay(500);
    { uint8_t d = 0x55; disp_cmd_data(0x3A, &d, 1); }
    disp_cmd(0x21);
    disp_cmd(0x29); delay(100);

    disp_fill(0, 0, DISP_W, DISP_H, C_LTGRAY);
    digitalWrite(PIN_DISP_BL, HIGH);
}

// ============================================================
//  CHROME — shared title bar, window header, transport, status
// ============================================================

static void draw_titlebar()
{
    disp_fill(0, 0, DISP_W, TITLEBAR_H, C_LTGRAY);
    const char *ind = g_playing ? (g_paused ? "||" : "|>") : "> ";
    disp_str(4,  7, ind,           C_BLACK, C_LTGRAY);
    disp_str(20, 7, "MUSIC PLAYER", C_BLACK, C_LTGRAY);
    disp_str(DISP_W - 62, 7, g_clock, C_BLACK, C_LTGRAY);
    // battery outline
    disp_fill(DISP_W-22,  9, 14, 7, C_BLACK);
    disp_fill(DISP_W-21, 10, 12, 5, C_LTGRAY);
    disp_fill(DISP_W-21, 10,  9, 5, C_BLACK);
    disp_fill(DISP_W-8,  11,  2, 3, C_BLACK);
}

static void draw_win_header(const char *title)
{
    disp_fill(0, WIN_Y, DISP_W, WIN_HDR_H, C_NAVY);
    disp_str(6,  WIN_Y + 6, "* ",   C_WHITE, C_NAVY);
    disp_str(18, WIN_Y + 6, title,  C_WHITE, C_NAVY);
    disp_str(DISP_W - 54, WIN_Y + 6, "-  [ ]  X", C_WHITE, C_NAVY);
}

static void draw_transport()
{
    disp_fill(0, TRANSPORT_Y, DISP_W, TRANSPORT_H, C_TBAR);
    disp_fill(0, TRANSPORT_Y, DISP_W, 1, C_WINBDR);

    // |<  at ~x=30
    disp_str(22, TRANSPORT_Y + 19, "|<", C_WHITE, C_TBAR);

    // centre play/pause box
    disp_fill(88, TRANSPORT_Y + 4, 64, TRANSPORT_H - 8, C_BLACK);
    disp_rect(88, TRANSPORT_Y + 4, 64, TRANSPORT_H - 8, C_DKGRAY);
    if (!g_paused)
        disp_char(115, TRANSPORT_Y + 16, '>', C_WHITE, C_BLACK, 2);
    else
        disp_str(108, TRANSPORT_Y + 19, "||", C_WHITE, C_BLACK);

    // (||) at ~x=168  — dedicated pause visual
    disp_str(164, TRANSPORT_Y + 19, "(|)", C_WHITE, C_TBAR);

    // >|  at ~x=208
    disp_str(206, TRANSPORT_Y + 19, ">|", C_WHITE, C_TBAR);
}

static void draw_statusbar(int cur, int total)
{
    disp_fill(0, STATUSBAR_Y, DISP_W, STATUSBAR_H, C_LTGRAY);
    disp_fill(0, STATUSBAR_Y, DISP_W, 1, C_WINBDR);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d Items", total);
    disp_str(4, STATUSBAR_Y + 6, buf, C_BLACK, C_LTGRAY);
    // mini progress fill
    int bw = 80, bh = 8;
    int bx = DISP_W - bw - 4, by = STATUSBAR_Y + 6;
    disp_fill(bx, by, bw, bh, C_WHITE);
    disp_rect(bx, by, bw, bh, C_BLACK);
    if (total > 0) disp_fill(bx+1, by+1, bw-2, bh-2, C_BLACK);
}

// ============================================================
//  VOLUME POPUP  — drawn over now-playing content area
//  Shows a horizontal bar + percentage, auto-hides after 1.5s
// ============================================================
static void draw_volume_popup()
{
    // Popup box centred horizontally at y=140
    const int PX = 20, PY = 136, PW = DISP_W - 40, PH = 36;
    disp_fill(PX,     PY,     PW,    PH,    C_VOLBG);
    disp_rect(PX,     PY,     PW,    PH,    C_WHITE);

    disp_str(PX + 6, PY + 5, "VOL", C_WHITE, C_VOLBG);

    // Bar
    const int BX = PX + 30, BY = PY + 8, BW = PW - 60, BH = 10;
    disp_fill(BX,    BY, BW, BH, C_DKGRAY);
    disp_rect(BX,    BY, BW, BH, C_WHITE);
    int fill = (int)((float)g_volume / 100.0f * (BW - 2));
    if (fill > 0) disp_fill(BX+1, BY+1, fill, BH-2, C_WHITE);

    // Percentage text
    char pct[6];
    snprintf(pct, sizeof(pct), "%d%%", g_volume);
    disp_str(BX + BW + 4, PY + 8, pct, C_WHITE, C_VOLBG);
}

static void show_volume_popup()
{
    g_vol_visible  = true;
    g_vol_hide_ms  = millis() + 1500;
    draw_volume_popup();
}

static void hide_volume_popup_if_needed()
{
    if (g_vol_visible && millis() >= g_vol_hide_ms) {
        g_vol_visible = false;
        if (g_screen == SCR_NOWPLAYING) {
            draw_nowplaying_content(false); // partial redraw, skip the art placeholder repaint
        }
    }
}

// Forward-declaration needed because draw_nowplaying_content calls draw_volume_popup
static void draw_nowplaying_content();

// ============================================================
//  SCREEN: MENU
// ============================================================
static const char *MENU_ITEMS[] = {
    "Songs", "Artists", "Albums", "Settings", "Shuffle Songs", "Now Playing"
};
static const char *MENU_ICONS[] = {">", ">", ">", ">", "~>", "dB"};

static void draw_menu()
{
    draw_titlebar();
    draw_win_header("MUSIC");
    disp_fill(0, CONTENT_Y, DISP_W, CONTENT_H, C_LTGRAY);

    for (int i = 0; i < 6; i++) {
        int ry = CONTENT_Y + i * ROW_H;
        bool sel = (i == g_menu_sel);
        uint16_t rbg = sel ? C_ROWSEL : C_ROWBG;
        uint16_t rfg = sel ? C_WHITE  : C_BLACK;
        disp_fill(0, ry, DISP_W, ROW_H, rbg);
        disp_fill(0, ry, DISP_W, 1,     C_MIDGRAY);
        disp_str_clip(ROW_TEXT_X, ry + 14, MENU_ITEMS[i], rfg, rbg, DISP_W - 30);
        disp_str(ARROW_X, ry + 14, MENU_ICONS[i], rfg, rbg);
    }
    disp_fill(0, CONTENT_Y + 6*ROW_H, DISP_W, 1, C_MIDGRAY);
    draw_statusbar(g_song_count, g_song_count);
    draw_transport();
}

// ============================================================
//  SCREEN: SONG LIST
// ============================================================
static void draw_song_list()
{
    draw_titlebar();
    draw_win_header("SONGS");

    if (g_song_count == 0) {
        disp_fill(0, CONTENT_Y, DISP_W, CONTENT_H, C_LTGRAY);
        disp_str(16, CONTENT_Y + 30, "NO SONGS FOUND", C_RED, C_LTGRAY, 1);
        disp_str(8,  CONTENT_Y + 54, "1. Check SD card is inserted", C_BLACK, C_LTGRAY);
        disp_str(8,  CONTENT_Y + 68, "2. Add .mp3/.wav/.flac files", C_BLACK, C_LTGRAY);
        disp_str(8,  CONTENT_Y + 96, "PREV = back to menu",    C_DKGRAY, C_LTGRAY);
        draw_statusbar(0, 0);
        draw_transport();
        return;
    }

    // Scrollbar
    int sb_x = DISP_W - 12;
    disp_fill(sb_x, CONTENT_Y, 12, CONTENT_H, C_DKGRAY);
    disp_rect(sb_x, CONTENT_Y, 12, CONTENT_H, C_WINBDR);
    if (g_song_count > VISIBLE_ROWS) {
        int th = max(14, CONTENT_H * VISIBLE_ROWS / g_song_count);
        int ty = CONTENT_Y + (CONTENT_H - th) * g_song_scroll
                 / max(1, g_song_count - VISIBLE_ROWS);
        disp_fill(sb_x+1, ty, 10, th, C_LTGRAY);
        disp_rect(sb_x+1, ty, 10, th, C_WINBDR);
    }

    for (int i = 0; i < VISIBLE_ROWS; i++) {
        int idx = g_song_scroll + i;
        int ry  = CONTENT_Y + i * ROW_H;

        if (idx >= g_song_count) {
            disp_fill(0, ry, sb_x, ROW_H, C_ROWBG);
            continue;
        }

        uint16_t rbg, rfg;
        if (idx == g_song_sel) {
            rbg = C_ROWSEL; rfg = C_WHITE;
        } else if (idx == g_now_idx) {
            rbg = C_ROWBG;  rfg = C_NAVY;
        } else {
            rbg = (i % 2 == 0) ? C_ROWBG : C_ROWALT;
            rfg = C_BLACK;
        }

        disp_fill(0, ry, sb_x, ROW_H, rbg);
        disp_fill(0, ry, sb_x, 1, C_MIDGRAY);
        // Now-playing indicator: small filled dot on left
        if (idx == g_now_idx) {
            disp_fill(3, ry + ROW_H/2 - 2, 4, 4, C_NAVY);
        }
        disp_str_clip(ROW_TEXT_X, ry + 14, g_songs[idx].title,
                      rfg, rbg, sb_x - ROW_TEXT_X - 2);
    }

    draw_statusbar(g_song_sel + 1, g_song_count);
    draw_transport();
}

// ============================================================
//  SCREEN: NOW PLAYING — content area only
//  Called for both full and partial redraws
// ============================================================
static void draw_nowplaying_content(bool full)
{
    // No art source without WiFi/HTTP — always draw the placeholder box.
    if (full) {
        disp_fill(ART_X, ART_Y, ART_W, ART_H, C_BLACK);
        disp_rect(ART_X, ART_Y, ART_W, ART_H, C_WINBDR);
    }

    // Bitrate badge (drawn over the art)
    if (g_now_idx >= 0 && g_songs[g_now_idx].bitrate_kbps > 0) {
        char br[16];
        snprintf(br, sizeof(br), "%dKBPS", g_songs[g_now_idx].bitrate_kbps);
        disp_fill(ART_X + 3, ART_Y + 3, 54, 12, C_BLACK);
        disp_str(ART_X + 5, ART_Y + 5, br, C_WHITE, C_BLACK);
    }

    // Side arrows
    disp_str(ART_X - 12, ART_Y + ART_H/2 - 4, "<", C_BLACK, C_LTGRAY);
    disp_str(ART_X + ART_W + 4, ART_Y + ART_H/2 - 4, ">", C_BLACK, C_LTGRAY);

    // Track info
    if (g_now_idx >= 0) {
        disp_fill(0, TRACK_Y,  DISP_W, 12, C_LTGRAY);
        disp_fill(0, ARTIST_Y, DISP_W, 10, C_LTGRAY);

        int tlen = strlen(g_songs[g_now_idx].title);
        int tx   = max(4, (DISP_W - tlen*6) / 2);
        disp_str_clip(tx, TRACK_Y, g_songs[g_now_idx].title, C_BLACK, C_LTGRAY, DISP_W - 8);

        int alen = strlen(g_songs[g_now_idx].artist);
        int ax   = max(4, (DISP_W - alen*6) / 2);
        disp_str_clip(ax, ARTIST_Y, g_songs[g_now_idx].artist, C_DKGRAY, C_LTGRAY, DISP_W - 8);
    }

    // Progress bar track
    disp_fill(PROG_X, PROG_Y, PROG_W, PROG_H, C_WHITE);
    disp_rect(PROG_X, PROG_Y, PROG_W, PROG_H, C_BLACK);

    draw_nowplaying_progress();
    if (g_vol_visible) draw_volume_popup();
}

static void draw_nowplaying_progress()
{
    int dur     = (g_now_idx >= 0) ? g_songs[g_now_idx].duration_s : 0;
    int elapsed = 0;

    if (g_playing && !g_paused) {
        elapsed = (int)((millis() - g_play_start - g_paused_accum) / 1000);
        if (elapsed > dur && dur > 0) elapsed = dur;
    } else if (g_playing && g_paused) {
        elapsed = (int)((g_pause_began - g_play_start - g_paused_accum) / 1000);
        if (elapsed > dur && dur > 0) elapsed = dur;
    }

    float frac = (dur > 0) ? (float)elapsed / dur : 0.0f;
    int   fill  = (int)(frac * (PROG_W - 4));

    // Clear bar interior
    disp_fill(PROG_X+1, PROG_Y+1, PROG_W-2, PROG_H-2, C_WHITE);
    if (fill > 0)
        disp_fill(PROG_X+2, PROG_Y+1, fill, PROG_H-2, C_BLACK);

    // Thumb knob
    int kx = PROG_X + 2 + fill - 3;
    if (kx < PROG_X + 2) kx = PROG_X + 2;
    disp_fill(kx, PROG_Y-2, 6, PROG_H+4, C_BLACK);
    disp_rect(kx, PROG_Y-2, 6, PROG_H+4, C_DKGRAY);

    // Time labels
    char el[8], du[8];
    fmt_time(el, sizeof(el), elapsed);
    fmt_time(du, sizeof(du), dur);
    disp_fill(PROG_X,    TIME_Y, 40, 8, C_LTGRAY);
    disp_fill(DISP_W-42, TIME_Y, 40, 8, C_LTGRAY);
    disp_str(PROG_X,     TIME_Y, el, C_BLACK, C_LTGRAY);
    disp_str(DISP_W-42,  TIME_Y, du, C_BLACK, C_LTGRAY);
}

static void draw_nowplaying(bool full)
{
    if (full) {
        draw_titlebar();
        draw_win_header("NOW PLAYING");
        disp_fill(0, CONTENT_Y, DISP_W, CONTENT_H, C_LTGRAY);
        draw_nowplaying_content(true); // Pass true
        draw_transport();
        draw_statusbar(g_now_idx + 1, g_song_count);
    } else {
        draw_nowplaying_progress();
        if (g_vol_visible) draw_volume_popup();
    }
}

// ============================================================
//  REDRAW DISPATCHER
// ============================================================
static void redraw(bool full = true)
{
    switch (g_screen) {
        case SCR_MENU:       draw_menu();          break;
        case SCR_SONGS:      draw_song_list();     break;
        case SCR_NOWPLAYING: draw_nowplaying(full); break;
    }
}

// ============================================================
//  NAVIGATION HELPERS
// ============================================================
static void go_back()
{
    switch (g_screen) {
        case SCR_NOWPLAYING:
            g_screen = SCR_SONGS;
            redraw(true);
            break;
        case SCR_SONGS:
            g_screen = SCR_MENU;
            redraw(true);
            break;
        case SCR_MENU:
        default:
            break;  // already at top
    }
}

// ============================================================
//  SD LIBRARY  — recursively scans every folder on the card (root,
//  /music, and anything nested under it), no ID3/FLAC tag parsing
//  yet: title is just the filename, artist/album are blank and
//  duration/bitrate are unknown until a track is played.
// ============================================================
#define SCAN_MAX_DEPTH 8

static void scan_dir(File dir, int depth)
{
    if (depth > SCAN_MAX_DEPTH) return;

    File entry = dir.openNextFile();
    while (entry && g_song_count < 100) {
        String full = entry.path();
        if (entry.isDirectory()) {
            File sub = SD.open(full);
            if (sub) {
                scan_dir(sub, depth + 1);
                sub.close();
            }
        } else {
            String lower = full;
            lower.toLowerCase();
            if (lower.endsWith(".mp3") || lower.endsWith(".wav") || lower.endsWith(".flac")) {
                int slash = full.lastIndexOf('/');
                String name = (slash >= 0) ? full.substring(slash + 1) : full;

                Song &s = g_songs[g_song_count];
                strncpy(s.title, name.c_str(), sizeof(s.title) - 1);
                s.title[sizeof(s.title) - 1] = '\0';
                strncpy(s.path, full.c_str(), sizeof(s.path) - 1);
                s.path[sizeof(s.path) - 1] = '\0';
                s.artist[0]      = '\0';
                s.album[0]       = '\0';
                s.duration_s     = 0;
                s.bitrate_kbps   = 0;
                g_song_count++;
            }
        }
        entry.close();
        entry = dir.openNextFile();
    }
}

static void fetch_song_list()
{
    // Loading indicator
    disp_fill(0, CONTENT_Y, DISP_W, CONTENT_H, C_LTGRAY);
    disp_str(50, CONTENT_Y + 70, "Loading...", C_NAVY, C_LTGRAY, 1);

    g_song_count = 0;

    File root = SD.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("[SD] Failed to open root directory");
        return;
    }

    scan_dir(root, 0);
    root.close();

    g_fetched = true;
    Serial.printf("[SD] %d songs found\n", g_song_count);
}

// ============================================================
//  PLAYBACK
// ============================================================
static void set_volume(int v)
{
    g_volume = max(0, min(100, v));
    g_audio.setVolume(g_volume * 21 / 100);
    Serial.printf("[VOL] %d%%\n", g_volume);
}

static void play_song(int idx)
{
    if (idx < 0 || idx >= g_song_count) return;
    const char *path = g_songs[idx].path;
    Serial.printf("[AUDIO] #%d %s\n", idx, path);

    g_audio.stopSong();
    g_audio.connecttoFS(SD, path);

    g_now_idx      = idx;
    g_playing      = true;
    g_paused       = false;
    g_play_start   = millis();
    g_paused_accum = 0;
    g_pause_began  = 0;
    g_vol_visible  = false;

    g_screen = SCR_NOWPLAYING;
    redraw(true);
}

static void toggle_pause()
{
    if (!g_playing) return;
    g_paused = !g_paused;
    g_audio.pauseResume();
    if (g_paused) {
        g_pause_began = millis();      // start counting paused time
    } else {
        g_paused_accum += millis() - g_pause_began;  // accumulate
        g_pause_began  = 0;
    }
    draw_transport();
    draw_titlebar();
}

static void play_next()
{
    if (g_now_idx < g_song_count - 1) play_song(g_now_idx + 1);
}

static void play_prev()
{
    if (g_now_idx > 0) play_song(g_now_idx - 1);
}

// ============================================================
//  BUTTONS
// ============================================================
static void init_buttons()
{
    int pins[5] = {PIN_BTN_PLAY, PIN_BTN_NEXT, PIN_BTN_PREV,
                   PIN_BTN_UP,   PIN_BTN_DOWN};
    for (int i = 0; i < 5; i++) pinMode(pins[i], INPUT_PULLUP);
}

// Returns short press 1-5, long press 11-15, 0 = nothing
static int poll_buttons()
{
    static uint32_t down_ms[5]    = {0,0,0,0,0};
    static bool     state[5]      = {false,false,false,false,false};
    static bool     long_fired[5] = {false,false,false,false,false};

    int pins[5] = {PIN_BTN_PLAY, PIN_BTN_NEXT, PIN_BTN_PREV,
                   PIN_BTN_UP,   PIN_BTN_DOWN};
    int ret = 0;
    uint32_t now = millis();

    for (int i = 0; i < 5; i++) {
        bool pressed = (digitalRead(pins[i]) == LOW);
        if (pressed && !state[i]) {
            down_ms[i]    = now;
            long_fired[i] = false;
        } else if (pressed && state[i]) {
            if (!long_fired[i] && now - down_ms[i] >= 600) {
                long_fired[i] = true;
                ret = 10 + (i + 1);
            }
        } else if (!pressed && state[i]) {
            if (!long_fired[i] && now - down_ms[i] >= 30)
                ret = i + 1;
        }
        state[i] = pressed;
    }
    return ret;
}

// ============================================================
//  INPUT HANDLER
//
//  NAVIGATION LOGIC (every screen):
//    PREV short      = back one screen
//    DOWN long       = back one screen  (thumb-friendly alternative)
//
//  MENU:
//    UP/DOWN         = scroll menu
//    PLAY            = enter item
//    NEXT/PREV       = global skip (works even from menu)
//
//  SONGS:
//    UP/DOWN         = scroll list
//    PLAY            = play selected
//    PREV short      = back to menu
//
//  NOW PLAYING:
//    PLAY            = play/pause
//    NEXT            = next track
//    PREV (< 3s)     = previous track
//    PREV (> 3s)     = restart song
//    UP              = volume up   (+5, popup)
//    DOWN short      = volume down (-5, popup)
//    DOWN long       = back to song list
//    NEXT long       = fast forward +10s
//    PREV long       = rewind -10s
// ============================================================
static void handle_input(int ev)
{
    if (ev == 0) return;
    Serial.printf("[BTN] ev=%d screen=%d\n", ev, (int)g_screen);

    const int P=1,NX=2,PR=3,UP=4,DN=5;
    const int LP=11,LN=12,LR=13,LU=14,LD=15;

    // Global: long DOWN always goes back
    if (ev == LD) { go_back(); return; }

    switch (g_screen) {

    // ── MENU ─────────────────────────────────────────────────
    case SCR_MENU:
        if      (ev == UP) { g_menu_sel = max(0, g_menu_sel-1); redraw(); }
        else if (ev == DN) { g_menu_sel = min(5, g_menu_sel+1); redraw(); }
        else if (ev == P) {
            switch (g_menu_sel) {
                case 0:   // Songs
                    g_screen = SCR_SONGS;
                    g_song_sel = 0; g_song_scroll = 0;
                    if (!g_fetched) fetch_song_list();
                    redraw(true);
                    break;
                case 4:   // Shuffle
                    if (g_song_count > 0) {
                        if (!g_fetched) fetch_song_list();
                        play_song(random(g_song_count));
                    }
                    break;
                case 5:   // Now Playing
                    if (g_now_idx >= 0) { g_screen = SCR_NOWPLAYING; redraw(true); }
                    break;
                default:
                    break;
            }
        }
        else if (ev == NX) { play_next(); }
        else if (ev == PR) { play_prev(); }
        break;

    // ── SONGS ─────────────────────────────────────────────────
    case SCR_SONGS:
        if (ev == UP) {
            if (g_song_sel > 0) {
                g_song_sel--;
                if (g_song_sel < g_song_scroll) g_song_scroll = g_song_sel;
                redraw();
            }
        }
        else if (ev == DN) {
            if (g_song_sel < g_song_count - 1) {
                g_song_sel++;
                if (g_song_sel >= g_song_scroll + VISIBLE_ROWS)
                    g_song_scroll = g_song_sel - VISIBLE_ROWS + 1;
                redraw();
            }
        }
        else if (ev == P)  { play_song(g_song_sel); }
        else if (ev == PR) { go_back(); }          // ← BACK TO MENU
        else if (ev == NX) { play_next(); }
        break;

    // ── NOW PLAYING ───────────────────────────────────────────
    case SCR_NOWPLAYING:
        if (ev == P) {
            toggle_pause();
        }
        else if (ev == NX) {
            play_next();
        }
        else if (ev == PR) {
            // < 3 seconds: restart or go prev; else back to song list
            int el = (int)((millis() - g_play_start - g_paused_accum) / 1000);
            if (el > 3) play_song(g_now_idx);
            else        play_prev();
        }
        else if (ev == UP) {
            set_volume(g_volume + 5);
            show_volume_popup();
        }
        else if (ev == DN) {
            set_volume(g_volume - 5);
            show_volume_popup();
        }
        else if (ev == LN) {  // long NEXT = fast forward
            uint32_t t = g_audio.getAudioCurrentTime();
            g_audio.setAudioPlayTime(t + 10);
        }
        else if (ev == LR) {  // long PREV = rewind
            int t = (int)g_audio.getAudioCurrentTime() - 10;
            g_audio.setAudioPlayTime(max(0, t));
        }
        // long DOWN already handled globally above as go_back()
        break;
    }
}

// ============================================================
//  SPLASH
// ============================================================
static void show_splash()
{
    disp_fill(0, 0, DISP_W, DISP_H, C_BLACK);
    disp_rect(2, 2, DISP_W-4, DISP_H-4, C_WINBDR);
    disp_fill(6, 6, DISP_W-12, 20, C_NAVY);
    disp_str(10, 12, "* MUSIC PLAYER", C_WHITE, C_NAVY);
    disp_fill(6, 30, DISP_W-12, DISP_H-60, C_LTGRAY);
    disp_str(40,  90, "ESP32-S3", C_BLACK, C_LTGRAY, 2);
    disp_str(58, 112, "iPOD",     C_NAVY,  C_LTGRAY, 2);
    disp_str(24, 155, "Mounting SD card...", C_BLACK, C_LTGRAY);
}

// ============================================================
//  SETUP
// ============================================================
void setup()
{
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n=== ESP32-S3 iPod ===");
    Serial.printf("[PSRAM] found=%d size=%u free=%u\n",
                   psramFound(), ESP.getPsramSize(), ESP.getFreePsram());

    init_buttons();
    init_display();
    show_splash();

    if (SD.begin()) {
        disp_str(24, 192, "SD card mounted", C_GREEN, C_LTGRAY);
        delay(400);
        disp_str(24, 208, "Scanning songs...", C_BLACK, C_LTGRAY);
        fetch_song_list();
        char buf[32];
        snprintf(buf, sizeof(buf), "%d songs found", g_song_count);
        disp_str(24, 224, buf, C_NAVY, C_LTGRAY);
        delay(600);
    } else {
        Serial.println("[SD] Card Mount Failed");
        disp_str(24, 192, "SD MOUNT FAILED!", C_RED, C_LTGRAY);
        delay(2000);
    }

    g_audio.setPinout(PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DOUT);
    g_audio.setVolume(g_volume * 21 / 100);

    g_screen = SCR_MENU;
    redraw(true);
}

// ============================================================
//  LOOP
// ============================================================
static uint32_t g_last_prog_ms  = 0;
static uint32_t g_last_clock_ms = 0;

void loop()
{
    // The audio loop pulls data from the SD file and feeds I2S
    g_audio.loop();

    int ev = poll_buttons();
    if (ev) {
        // Stop audio processing for a tiny fraction of a second to prevent SPI jitter
        handle_input(ev);
    }

    uint32_t now = millis();

    // Only update the progress bar once a second to reduce flicker
    if (g_screen == SCR_NOWPLAYING) {
        if (g_playing && !g_paused && now - g_last_prog_ms >= 1000) {
            g_last_prog_ms = now;
            draw_nowplaying(false); // Partial update
        }
        hide_volume_popup_if_needed();
    }

    // Update clock every 30 seconds
    if (now - g_last_clock_ms >= 30000) {
        g_last_clock_ms = now;
        uint32_t h = (now / 3600000) % 24;
        uint32_t m = (now / 60000) % 60;
        snprintf(g_clock, sizeof(g_clock), "%02lu:%02lu", (unsigned long)h, (unsigned long)m);
        draw_titlebar();
    }
    
    // Tiny yield to keep the OS stable
    yield();
}

// ============================================================
//  AUDIO CALLBACKS
// ============================================================
void audio_info(const char *info)       { Serial.printf("[A] %s\n", info); }
void audio_eof_mp3(const char *info)
{
    Serial.printf("[A] EOF: %s\n", info);
    if (g_now_idx < g_song_count - 1) {
        delay(200);
        play_song(g_now_idx + 1);
    } else {
        g_playing = false;
        g_paused  = false;
        g_screen  = SCR_SONGS;
        redraw(true);
    }
}
