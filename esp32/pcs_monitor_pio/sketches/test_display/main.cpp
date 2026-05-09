// =============================================================================
// test_display — Hardware sketch: Ideaspark ESP32 ST7789 135x240 screen
//
// Cicla por 3 pantallas cada 3 segundos para verificar que el display funciona:
//   Pantalla 1 — Estado del sistema (dots de estado, uptime)
//   Pantalla 2 — Valores del inversor (datos hardcodeados)
//   Pantalla 3 — Valores de batería (datos hardcodeados, campos LWS completos)
//
// Sin WiFi, sin Modbus — test puro de display.
//
// Wiring (Ideaspark ESP32 1.14" ST7789, del datasheet oficial):
//   GPIO23 → MOSI
//   GPIO18 → SCLK
//   GPIO15 → CS
//   GPIO2  → DC
//   GPIO4  → RST   ← NOTA: en el firmware real GPIO4 es LCD RST del Ideaspark
//                           No usar para RS-485 DE+RE (usar GPIO5 en su lugar)
//   GPIO32 → BLK (backlight)
// =============================================================================

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// ---------------------------------------------------------------------------
// Pin definitions
// ---------------------------------------------------------------------------
#define LCD_MOSI  23
#define LCD_SCLK  18
#define LCD_CS    15
#define LCD_DC     2
#define LCD_RST    4
#define LCD_BLK   32

// Display dimensions (135 wide × 240 tall, landscape = 240 wide × 135 tall)
#define SCREEN_W  240
#define SCREEN_H  135

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------
#define COLOR_BG       0x0000   // black
#define COLOR_TITLE    0x07FF   // cyan
#define COLOR_LABEL    0x8410   // grey
#define COLOR_VALUE    0xFFFF   // white
#define COLOR_OK       0x07E0   // green
#define COLOR_WARN     0xFFE0   // yellow
#define COLOR_FAULT    0xF800   // red

Adafruit_ST7789 tft = Adafruit_ST7789(LCD_CS, LCD_DC, LCD_RST);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
void drawHeader(const char* title) {
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(COLOR_TITLE);
    tft.setTextSize(2);
    tft.setCursor(4, 4);
    tft.print(title);
    // Separator line
    tft.drawFastHLine(0, 22, SCREEN_W, COLOR_TITLE);
}

void drawRow(uint8_t row, const char* label, const char* value, uint16_t valueColor = COLOR_VALUE) {
    uint8_t y = 28 + row * 18;
    tft.setTextColor(COLOR_LABEL);
    tft.setTextSize(1);
    tft.setCursor(4, y);
    tft.print(label);
    tft.setTextColor(valueColor);
    tft.setCursor(110, y);
    tft.print(value);
}

void drawStatusDot(uint8_t x, uint8_t y, const char* label, bool ok) {
    tft.fillCircle(x, y, 5, ok ? COLOR_OK : COLOR_FAULT);
    tft.setTextColor(COLOR_LABEL);
    tft.setTextSize(1);
    tft.setCursor(x + 9, y - 4);
    tft.print(label);
}

// ---------------------------------------------------------------------------
// Screen 1 — System status
// ---------------------------------------------------------------------------
void screenStatus() {
    drawHeader("PCS Monitor");

    // Status dots
    drawStatusDot(10, 40, "WiFi",  true);
    drawStatusDot(10, 60, "MQTT",  true);
    drawStatusDot(10, 80, "Modbus",true);
    drawStatusDot(10, 100,  "BMS", false);  // BMS not connected yet

    // Uptime
    char buf[24];
    snprintf(buf, sizeof(buf), "%lus", millis() / 1000);
    tft.setTextColor(COLOR_LABEL); tft.setTextSize(1);
    tft.setCursor(150, 40); tft.print("Uptime:");
    tft.setTextColor(COLOR_VALUE);
    tft.setCursor(150, 52); tft.print(buf);

    // Firmware label
    tft.setTextColor(COLOR_LABEL); tft.setTextSize(1);
    tft.setCursor(150, 80); tft.print("test_display");
    tft.setCursor(150, 92); tft.print("v0.1");
}

// ---------------------------------------------------------------------------
// Screen 2 — Inverter values (dummy data)
// ---------------------------------------------------------------------------
void screenInverter() {
    drawHeader("Inverter");

    // Status indicator
    tft.setTextColor(COLOR_OK); tft.setTextSize(1);
    tft.setCursor(160, 6); tft.print("RUNNING");

    drawRow(0, "Freq",     "50.01 Hz");
    drawRow(1, "V_a",      "231.4 V");
    drawRow(2, "P_inv",    "12.30 kW",  COLOR_OK);
    drawRow(3, "PF",       "0.98");
    drawRow(4, "DC Volt",  "612.0 V");
    drawRow(5, "Grid P",   "-5.20 kW",  COLOR_WARN);
}

// ---------------------------------------------------------------------------
// Screen 3 — BMS values (dummy data)
// ---------------------------------------------------------------------------
void screenBMS() {
    drawHeader("Battery");

    // SOC bar
    uint8_t soc = 75;
    tft.setTextColor(COLOR_VALUE); tft.setTextSize(2);
    tft.setCursor(4, 28);
    tft.printf("SOC: %d%%", soc);

    tft.drawRect(4, 52, 180, 14, COLOR_LABEL);
    uint16_t barColor = soc > 50 ? COLOR_OK : (soc > 20 ? COLOR_WARN : COLOR_FAULT);
    tft.fillRect(5, 53, (int)(180 * soc / 100.0f), 12, barColor);

    drawRow(3, "Voltage",    "496.0 V");
    drawRow(4, "Current",    "40.0 A",   COLOR_OK);
    drawRow(5, "Temp avg",   "24.5 C");
    drawRow(6, "Temp max",   "26.5 C");
    drawRow(7, "Cell V max", "3.310 V");
    drawRow(8, "Cell V min", "3.280 V");
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[Boot] test_display starting...");

    // Backlight on
    pinMode(LCD_BLK, OUTPUT);
    digitalWrite(LCD_BLK, HIGH);

    // Init display — landscape, 135x240 → rotated to 240x135
    tft.init(135, 240);
    tft.setRotation(3);  // landscape, connector on right
    tft.fillScreen(COLOR_BG);

    Serial.println("[Boot] Display ready");
}

uint8_t page = 0;
unsigned long lastSwitch = 0;

void loop() {
    if (millis() - lastSwitch >= 3000) {
        lastSwitch = millis();

        switch (page % 3) {
            case 0: screenStatus();   Serial.println("[Display] Screen: Status");   break;
            case 1: screenInverter(); Serial.println("[Display] Screen: Inverter"); break;
            case 2: screenBMS();      Serial.println("[Display] Screen: BMS");      break;
        }
        page++;
    }
}
