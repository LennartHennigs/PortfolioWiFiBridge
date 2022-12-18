/* ------------------------------------------------- */
#include <Wire.h>
#include <Adafruit_SSD1306.h>

/* ------------------------------------------------- */

#define DISPLAY_I2C     0x3C
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64
#define DISPLAY_LINE    10

/* ------------------------------------------------- */
Adafruit_SSD1306 display(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, -1);
/* ------------------------------------------------- */

void clearDisplay();
void initDisplay();
void clearRow(byte row, bool update);
void printToRow(byte row, String str, bool);

/* ------------------------------------------------- */

void initDisplay() {
  display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_I2C);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.cp437(true);
  display.display();
  clearDisplay();
}

/* ------------------------------------------------- */

void clearDisplay() {
  display.clearDisplay();
  display.display();
}

/* ------------------------------------------------- */

void clearRow(byte row, bool update = true) {
  display.fillRect(0, row * DISPLAY_LINE, DISPLAY_WIDTH, DISPLAY_LINE, SSD1306_BLACK);
  if (update) display.display();
}

/* ------------------------------------------------- */

void printToRow(byte row, String str, bool clear = true) {
  if (clear) clearRow(row, false);
  display.setCursor(0, (row * DISPLAY_LINE) + 1);
  display.print(str);
  display.display();
  // if its important for the screen, it is important for the console
  Serial.println(str);
}

/* ------------------------------------------------- */
