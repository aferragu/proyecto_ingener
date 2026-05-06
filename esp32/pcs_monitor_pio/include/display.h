#pragma once
#include <ArduinoJson.h>

// Display LCD ST7789 1.14" (ideaspark ESP32)
// Pines: MOSI=23, SCLK=18, CS=15, BLK=32
// Librería: TFT_eSPI (instalar desde Library Manager)
// En User_Setup.h de TFT_eSPI configurar:
//   #define ST7789_DRIVER
//   #define TFT_WIDTH  135
//   #define TFT_HEIGHT 240
//   #define TFT_MOSI   23
//   #define TFT_SCLK   18
//   #define TFT_CS     15
//   #define TFT_DC     -1   // no usado en este módulo
//   #define TFT_RST    -1
//   #define TFT_BL     32

void displayInit();
void displayUpdate(JsonDocument& telemetry, bool mqttConnected);
