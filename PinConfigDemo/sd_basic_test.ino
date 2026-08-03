/*************************************************************************
  PROJECT: ESP32-S3 microSD Basic Test
  BOARD:   Edgehax ESP32-S3-WROOM-1 N16R8, built-in microSD slot

  CONFIRMED WORKING CONFIG:
   - Board (Tools menu): "ESP32S3 Dev Module" (NOT the "Octal" preset -
     that variant boot-loops on this board's PSRAM config)
   - PSRAM: as needed for your sketch (Disabled is fine for SD-only use)
   - Upload Mode: UART0 / Hardware CDC
   - USB CDC On Boot: Disabled (this board is flashed/monitored via the
     UART bridge chip, which shows up as /dev/ttyUSB0, not native USB)
   - SD pins: stock ESP32-S3 default SPI pins - CS=10, SCK=12, MISO=13,
     MOSI=11 - no custom SPI.begin() needed, plain SD.begin() works.

  Does: mount card, list root directory, write /hello.txt, read it back.
 *************************************************************************/

#include "FS.h"
#include "SD.h"
#include "SPI.h"

void listDir(fs::FS &fs, const char *dirname, uint8_t levels) {
  Serial.printf("Listing directory: %s\n", dirname);

  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    Serial.println("Failed to open directory");
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.printf("  DIR : %s\n", file.name());
      if (levels) listDir(fs, file.path(), levels - 1);
    } else {
      Serial.printf("  FILE: %-20s SIZE: %u\n", file.name(), file.size());
    }
    file = root.openNextFile();
  }
}

void writeFile(fs::FS &fs, const char *path, const char *message) {
  Serial.printf("Writing file: %s\n", path);
  File file = fs.open(path, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  Serial.println(file.print(message) ? "File written" : "Write failed");
  file.close();
}

void readFile(fs::FS &fs, const char *path) {
  Serial.printf("Reading file: %s\n", path);
  File file = fs.open(path);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }
  Serial.print("Content: ");
  while (file.available()) Serial.write(file.read());
  Serial.println();
  file.close();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==== EDGEHAX ESP32-S3 microSD Basic Test ====");

  if (!SD.begin()) {
    Serial.println("Card Mount Failed");
    return;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  switch (cardType) {
    case CARD_MMC:  Serial.println("MMC");  break;
    case CARD_SD:   Serial.println("SDSC"); break;
    case CARD_SDHC: Serial.println("SDHC"); break;
    default:        Serial.println("UNKNOWN"); break;
  }
  Serial.printf("SD Card Size: %lluMB\n", SD.cardSize() / (1024 * 1024));

  Serial.println();
  listDir(SD, "/", 0);

  Serial.println();
  writeFile(SD, "/hello.txt", "Hello World!\n");
  readFile(SD, "/hello.txt");

  Serial.println("\n==== Done ====");
}

void loop() {
}
