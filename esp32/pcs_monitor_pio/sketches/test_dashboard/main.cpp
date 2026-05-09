// =============================================================================
// test_dashboard — Telemetría real → ThingsBoard + display ST7789
//
// Lee el inversor SinoSoar SP6030 y el BMS LWS vía Modbus RTU, publica
// telemetría a ThingsBoard y muestra datos en pantalla ST7789.
//
// Pantallas:
//   1 — Estado del sistema (dots WiFi/MQTT/Modbus/BMS, SOC, carga)
//   2 — Flujo de potencia (inversor, red, carga, frecuencia, PF)
//   3 — Batería (SOC barra, tensión, corriente, temperatura)
//
// Wiring:
//   MAX485 DI  → GPIO17
//   MAX485 RO  → GPIO16
//   MAX485 DE+RE → GPIO5
//   MAX485 A/B → RS-485 bus (inversor + BMS en paralelo)
//   ST7789 MOSI → GPIO23, SCLK → GPIO18, CS → GPIO15
//   ST7789 DC   → GPIO2,  RST  → GPIO4,  BLK → GPIO32
//
// Device IDs: inversor = MODBUS_DEVICE_ID, BMS = BMS_MODBUS_DEVICE_ID (config.h)
// Credenciales: credentials.h
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "config.h"
#include "credentials.h"
#include "inverter.h"
#include "bms.h"

// ---------------------------------------------------------------------------
// ThingsBoard
// ---------------------------------------------------------------------------
#define TB_TOKEN         TB_ACCESS_TOKEN

// ---------------------------------------------------------------------------
// Display pins (Ideaspark hardwired)
// ---------------------------------------------------------------------------
#define LCD_MOSI  23
#define LCD_SCLK  18
#define LCD_CS    15
#define LCD_DC     2
#define LCD_RST    4
#define LCD_BLK   32
#define SCREEN_W  240
#define SCREEN_H  135

// ---------------------------------------------------------------------------
// Intervals
// ---------------------------------------------------------------------------
#define POLL_INTERVAL_MS    5000
#define DISPLAY_INTERVAL_MS 3000

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------
#define C_BG    0x0000
#define C_TITLE 0x07FF
#define C_LABEL 0x8410
#define C_VALUE 0xFFFF
#define C_OK    0x07E0
#define C_WARN  0xFFE0
#define C_FAULT 0xF800

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
WiFiClient      wifiClient;
PubSubClient    mqtt(wifiClient);
Adafruit_ST7789 tft = Adafruit_ST7789(LCD_CS, LCD_DC, LCD_RST);
// bmsData defined in lib/bms/src/bms.cpp — accessible via bms.h

bool wifiOk   = false;
bool mqttOk   = false;
bool modbusOk = false;   // true if last Modbus poll had at least one success
bool canOk    = false;   // true if BMS data is valid

// Live values shown on display — updated each poll
float disp_p_inv   = 0.0f;
float disp_grid_p  = 0.0f;
float disp_load_p  = 0.0f;
float disp_freq    = 0.0f;
float disp_v_a     = 0.0f;
float disp_pf      = 0.0f;
float disp_soc     = 0.0f;
float disp_bms_v   = 0.0f;
float disp_bms_i   = 0.0f;
float disp_bms_t   = 0.0f;

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------
void drawHeader(const char* title, const char* subtitle = nullptr) {
    tft.fillScreen(C_BG);
    tft.setTextColor(C_TITLE); tft.setTextSize(2);
    tft.setCursor(4, 4); tft.print(title);
    if (subtitle) {
        tft.setTextColor(C_LABEL); tft.setTextSize(1);
        tft.setCursor(SCREEN_W - strlen(subtitle) * 6 - 4, 9);
        tft.print(subtitle);
    }
    tft.drawFastHLine(0, 22, SCREEN_W, C_TITLE);
}

void drawRow(uint8_t row, const char* label, const char* value,
             uint16_t valueColor = C_VALUE) {
    uint8_t y = 28 + row * 18;
    tft.setTextColor(C_LABEL); tft.setTextSize(1);
    tft.setCursor(4, y); tft.print(label);
    tft.setTextColor(valueColor);
    tft.setCursor(120, y); tft.print(value);
}

void drawStatusDot(uint8_t x, uint8_t y, const char* label, bool ok) {
    tft.fillCircle(x, y, 5, ok ? C_OK : C_FAULT);
    tft.setTextColor(C_LABEL); tft.setTextSize(1);
    tft.setCursor(x + 9, y - 4); tft.print(label);
}

// ---------------------------------------------------------------------------
// Screen 1 — Status
// ---------------------------------------------------------------------------
void screenStatus() {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lus", millis() / 1000);
    drawHeader("PCS Monitor", buf);

    drawStatusDot(10,  40, "WiFi",   wifiOk);
    drawStatusDot(10,  60, "MQTT",   mqttOk);
    drawStatusDot(10,  80, "Modbus", modbusOk);
    drawStatusDot(10, 100, "BMS",    canOk);

    tft.setTextColor(C_LABEL); tft.setTextSize(1);
    tft.setCursor(130, 40); tft.print("Load:");
    snprintf(buf, sizeof(buf), "%.1fkW", disp_load_p);
    tft.setTextColor(C_VALUE);
    tft.setCursor(130, 52); tft.print(buf);

    tft.setTextColor(C_LABEL);
    tft.setCursor(130, 70); tft.print("SOC:");
    snprintf(buf, sizeof(buf), "%.0f%%", bmsData.soc_pct);
    tft.setTextColor(canOk ? (bmsData.soc_pct > 20 ? C_OK : C_WARN) : C_LABEL);
    tft.setCursor(130, 82); tft.print(canOk ? buf : "--");
}

// ---------------------------------------------------------------------------
// Screen 2 — Power flow
// ---------------------------------------------------------------------------
void screenPower() {
    drawHeader("Power Flow");
    char buf[24];

    snprintf(buf, sizeof(buf), modbusOk ? "%.2f kW" : "--", disp_p_inv);
    drawRow(0, "Inv Output",  buf, C_OK);

    snprintf(buf, sizeof(buf), modbusOk ? "%.2f kW" : "--", disp_grid_p);
    drawRow(1, "Grid Import", buf, disp_grid_p > 30 ? C_WARN : C_VALUE);

    snprintf(buf, sizeof(buf), modbusOk ? "%.2f kW" : "--", disp_load_p);
    drawRow(2, "Load",        buf);

    snprintf(buf, sizeof(buf), modbusOk ? "%.2f Hz" : "--", disp_freq);
    drawRow(3, "Frequency",   buf);

    snprintf(buf, sizeof(buf), modbusOk ? "%.1f V"  : "--", disp_v_a);
    drawRow(4, "Grid Va",     buf);

    snprintf(buf, sizeof(buf), modbusOk ? "%.2f"    : "--", disp_pf);
    drawRow(5, "Power Factor",buf);
}

// ---------------------------------------------------------------------------
// Screen 3 — Battery
// ---------------------------------------------------------------------------
void screenBattery() {
    drawHeader("Battery");
    char buf[24];

    if (canOk) {
        snprintf(buf, sizeof(buf), "SOC: %.0f%%", bmsData.soc_pct);
        tft.setTextColor(C_VALUE); tft.setTextSize(2);
        tft.setCursor(4, 28); tft.print(buf);
        tft.drawRect(4, 52, 180, 14, C_LABEL);
        uint16_t barColor = bmsData.soc_pct > 50 ? C_OK :
                           (bmsData.soc_pct > 20 ? C_WARN : C_FAULT);
        tft.fillRect(5, 53, (int)(180 * bmsData.soc_pct / 100.0f), 12, barColor);
        snprintf(buf, sizeof(buf), "%.1f V", bmsData.voltage_v);
        drawRow(3, "Voltage", buf);
        snprintf(buf, sizeof(buf), "%.1f A", bmsData.current_a);
        drawRow(4, "Current", buf, bmsData.current_a >= 0 ? C_OK : C_WARN);
        snprintf(buf, sizeof(buf), "%.1f C", bmsData.temp_avg_c);
        drawRow(5, "Temp avg", buf, bmsData.temp_avg_c > 35 ? C_WARN : C_VALUE);
    } else {
        tft.setTextColor(C_FAULT); tft.setTextSize(2);
        tft.setCursor(4, 50); tft.print("BMS");
        tft.setCursor(4, 74); tft.print("not connected");
    }
}

// ---------------------------------------------------------------------------
// WiFi / MQTT
// ---------------------------------------------------------------------------
void connectWiFi() {
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    uint8_t attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500); Serial.print("."); attempts++;
    }
    wifiOk = (WiFi.status() == WL_CONNECTED);
    Serial.printf(wifiOk ? "\n[WiFi] IP: %s\n" : "\n[WiFi] Failed\n",
                  WiFi.localIP().toString().c_str());
}

void connectMQTT() {
    if (!wifiOk) return;
    mqtt.setServer(TB_HOST, TB_PORT);
    mqtt.setBufferSize(2048);
    String clientId = "ESP32_dashboard_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.print("[MQTT] Connecting...");
    if (mqtt.connect(clientId.c_str(), TB_TOKEN, nullptr)) {
        mqttOk = true; Serial.println(" OK");
    } else {
        mqttOk = false; Serial.printf(" failed rc=%d\n", mqtt.state());
    }
}

// ---------------------------------------------------------------------------
// Poll inverter
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[Boot] test_dashboard starting...");

    // Display
    pinMode(LCD_BLK, OUTPUT); digitalWrite(LCD_BLK, HIGH);
    tft.init(135, 240); tft.setRotation(3); tft.fillScreen(C_BG);
    tft.setTextColor(C_LABEL); tft.setTextSize(1);
    tft.setCursor(4, 60); tft.print("Connecting...");

    // RS-485
    Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    inverterInit(Serial2, RS485_DE_RE_PIN);
    bmsInit(Serial2, RS485_DE_RE_PIN);

    connectWiFi();
    connectMQTT();
    tft.fillScreen(C_BG);
    Serial.println("[Boot] Ready");
}

uint8_t page = 0;
unsigned long lastPoll    = 0;
unsigned long lastDisplay = 0;

void loop() {
    if (WiFi.status() != WL_CONNECTED) { connectWiFi(); }
    if (wifiOk && !mqtt.connected())   { connectMQTT(); mqttOk = mqtt.connected(); }
    if (mqttOk) mqtt.loop();

    if (millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();

        JsonDocument doc;
        pollModbus(doc);
        pollBMS(doc);

        // Update display vars from doc
        modbusOk = doc["running"].is<int>();
        canOk    = doc["bms_soc_pct"].is<float>();
        disp_p_inv  = doc["p_inv_kw"]   | 0.0f;
        disp_freq   = doc["freq_hz"]     | 0.0f;
        disp_v_a    = doc["v_a"]         | 0.0f;
        disp_pf     = doc["pf_total"]    | 0.0f;
        disp_grid_p = doc["grid_p_kw"]   | 0.0f;
        disp_load_p = doc["load_p_kw"]   | 0.0f;
        disp_soc    = doc["bms_soc_pct"] | 0.0f;
        disp_bms_v  = doc["bms_voltage_v"]| 0.0f;
        disp_bms_i  = doc["bms_current_a"]| 0.0f;
        disp_bms_t  = doc["bms_temp_avg_c"]| 0.0f;

        if (mqttOk) {
            char payload[2048];
            serializeJson(doc, payload, sizeof(payload));
            Serial.printf("[MQTT] Payload %d bytes\n", strlen(payload));
            bool ok = mqtt.publish("v1/devices/me/telemetry", payload);
            Serial.printf("[MQTT] Publish %s\n", ok ? "OK" : "FAIL");
        }
    }

    if (millis() - lastDisplay >= DISPLAY_INTERVAL_MS) {
        lastDisplay = millis();
        switch (page % 3) {
            case 0: screenStatus();  break;
            case 1: screenPower();   break;
            case 2: screenBattery(); break;
        }
        page++;
    }
}
