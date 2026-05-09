// =============================================================================
// test_tb — Telemetría simulada → ThingsBoard + display ST7789
//
// Qué hace:
//   1. Conecta WiFi y ThingsBoard
//   2. Envía todos los keys de telemetría que manda el firmware real, con valores
//      que derivan lentamente de forma realista — usalo para armar el dashboard TB
//   3. Cicla el display ST7789 por 3 pantallas cada 3s con los mismos datos
//      simulados — usalo para validar el layout del display
//
// Simula: inversor SP6030 (AC, DC, grid, load) + BMS LWS (todos los campos)
// No requiere hardware más allá de WiFi y el display.
//
// Wiring (Ideaspark ESP32 1.14" ST7789):
//   GPIO23 → MOSI, GPIO18 → SCLK, GPIO15 → CS
//   GPIO2  → DC,   GPIO4  → RST,  GPIO32 → BLK
//
// Credenciales: credentials.h
// =============================================================================


#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// ---------------------------------------------------------------------------
// Credentials
// ---------------------------------------------------------------------------
#include "config.h"   // TB_HOST, TB_PORT
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
#define PUBLISH_INTERVAL_MS  5000
#define DISPLAY_INTERVAL_MS  3000

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------
#define C_BG      0x0000   // black
#define C_TITLE   0x07FF   // cyan
#define C_LABEL   0x8410   // grey
#define C_VALUE   0xFFFF   // white
#define C_OK      0x07E0   // green
#define C_WARN    0xFFE0   // yellow
#define C_FAULT   0xF800   // red

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
WiFiClient         wifiClient;
PubSubClient       mqtt(wifiClient);
Adafruit_ST7789    tft = Adafruit_ST7789(LCD_CS, LCD_DC, LCD_RST);

bool wifiOk  = false;
bool mqttOk  = false;

// ---------------------------------------------------------------------------
// Simulated state
// ---------------------------------------------------------------------------
struct Sim {
    // Inverter
    float soc          = 75.0f;
    float p_inv        = 12.0f;
    float grid_p       = 3.0f;
    float load_p       = 9.0f;
    float freq         = 50.0f;
    float v_phase      = 231.0f;
    float pf           = 0.98f;
    // BMS — LWS Modbus fields
    float bms_v        = 496.0f;
    float bms_i        = 40.0f;    // positive = charging
    float bms_temp_avg = 24.0f;
    float bms_temp_max = 26.0f;
    float bms_temp_min = 22.0f;
    float bms_temp_fet = 28.0f;
    float bms_cell_v_max = 3.31f;
    float bms_cell_v_min = 3.28f;
    float bms_max_chg_a   = 200.0f;
    float bms_max_dischg_a= 200.0f;
    float bms_chg_cutoff  = 537.6f;
    float bms_dischg_cutoff = 432.0f;
} sim;

float drift(float v, float step, float lo, float hi) {
    v += (random(-100, 100) / 100.0f) * step;
    return constrain(v, lo, hi);
}

void updateSim() {
    sim.soc            = drift(sim.soc,          0.2f,  10.0f,  95.0f);
    sim.bms_v          = drift(sim.bms_v,         1.0f, 450.0f, 540.0f);
    sim.bms_i          = drift(sim.bms_i,         5.0f,-200.0f, 200.0f);
    sim.bms_temp_avg   = drift(sim.bms_temp_avg,  0.2f,  15.0f,  45.0f);
    sim.bms_temp_max   = sim.bms_temp_avg + 2.0f;
    sim.bms_temp_min   = sim.bms_temp_avg - 2.0f;
    sim.bms_temp_fet   = sim.bms_temp_avg + 4.0f;
    sim.bms_cell_v_max = drift(sim.bms_cell_v_max, 0.005f, 3.0f, 3.65f);
    sim.bms_cell_v_min = sim.bms_cell_v_max - drift(0.03f, 0.002f, 0.01f, 0.08f);
    sim.p_inv          = drift(sim.p_inv,         1.0f,   0.0f,  30.0f);
    sim.grid_p         = drift(sim.grid_p,         1.0f,   0.0f,  50.0f);
    sim.load_p         = drift(sim.load_p,         0.5f,   1.0f,  20.0f);
    sim.freq           = drift(sim.freq,          0.02f,  49.8f,  50.2f);
    sim.v_phase        = drift(sim.v_phase,        0.5f, 220.0f, 240.0f);
    sim.pf             = drift(sim.pf,            0.01f,  0.85f,   1.0f);
}

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------
void drawHeader(const char* title, const char* subtitle = nullptr) {
    tft.fillScreen(C_BG);
    tft.setTextColor(C_TITLE);
    tft.setTextSize(2);
    tft.setCursor(4, 4);
    tft.print(title);
    if (subtitle) {
        tft.setTextColor(C_LABEL);
        tft.setTextSize(1);
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
// Screen 1 — System status
// ---------------------------------------------------------------------------
void screenStatus() {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lus", millis() / 1000);
    drawHeader("PCS Monitor", buf);

    drawStatusDot(10,  40, "WiFi",  wifiOk);
    drawStatusDot(10,  60, "MQTT",  mqttOk);
    drawStatusDot(10,  80, "Modbus",false);   // not tested yet
    drawStatusDot(10, 100, "BMS",   false);   // CAN not connected yet

    tft.setTextColor(C_LABEL); tft.setTextSize(1);
    tft.setCursor(130, 40); tft.print("SOC:");
    snprintf(buf, sizeof(buf), "%d%%", (int)sim.soc);
    tft.setTextColor(sim.soc > 20 ? C_OK : C_WARN);
    tft.setCursor(130, 52); tft.print(buf);

    tft.setTextColor(C_LABEL);
    tft.setCursor(130, 70); tft.print("Load:");
    snprintf(buf, sizeof(buf), "%.1fkW", sim.load_p);
    tft.setTextColor(C_VALUE);
    tft.setCursor(130, 82); tft.print(buf);
}

// ---------------------------------------------------------------------------
// Screen 2 — Power flow
// ---------------------------------------------------------------------------
void screenPower() {
    drawHeader("Power Flow");
    char buf[24];

    snprintf(buf, sizeof(buf), "%.2f kW", sim.p_inv);
    drawRow(0, "Inv Output", buf, C_OK);

    snprintf(buf, sizeof(buf), "%.2f kW", sim.grid_p);
    drawRow(1, "Grid Import", buf, sim.grid_p > 30 ? C_WARN : C_VALUE);

    snprintf(buf, sizeof(buf), "%.2f kW", sim.load_p);
    drawRow(2, "Load", buf);

    snprintf(buf, sizeof(buf), "%.2f Hz", sim.freq);
    drawRow(3, "Frequency", buf);

    snprintf(buf, sizeof(buf), "%.1f V", sim.v_phase);
    drawRow(4, "Grid Va", buf);

    snprintf(buf, sizeof(buf), "%.2f", sim.pf);
    drawRow(5, "Power Factor", buf);
}

// ---------------------------------------------------------------------------
// Screen 3 — Battery
// ---------------------------------------------------------------------------
void screenBattery() {
    drawHeader("Battery");
    char buf[24];

    // SOC value + bar
    snprintf(buf, sizeof(buf), "SOC: %d%%", (int)sim.soc);
    tft.setTextColor(C_VALUE); tft.setTextSize(2);
    tft.setCursor(4, 28); tft.print(buf);

    tft.drawRect(4, 52, 180, 14, C_LABEL);
    uint16_t barColor = sim.soc > 50 ? C_OK : (sim.soc > 20 ? C_WARN : C_FAULT);
    tft.fillRect(5, 53, (int)(180 * sim.soc / 100.0f), 12, barColor);

    snprintf(buf, sizeof(buf), "%.1f V", sim.bms_v);
    drawRow(3, "Voltage", buf);

    snprintf(buf, sizeof(buf), "%.1f A", sim.bms_i);
    drawRow(4, "Current", buf, sim.bms_i >= 0 ? C_OK : C_WARN);

    snprintf(buf, sizeof(buf), "%.1f C", sim.bms_temp_avg);
    drawRow(5, "Temp avg", buf, sim.bms_temp_avg > 35 ? C_WARN : C_VALUE);
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
    if (wifiOk)
        Serial.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
    else
        Serial.println("\n[WiFi] Failed — continuing without WiFi");
}

void connectMQTT() {
    if (!wifiOk) return;
    mqtt.setServer(TB_HOST, TB_PORT);
    mqtt.setBufferSize(2048);
    String clientId = "ESP32_test_tb_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    Serial.print("[MQTT] Connecting...");
    if (mqtt.connect(clientId.c_str(), TB_TOKEN, nullptr)) {
        mqttOk = true;
        Serial.println(" OK");
    } else {
        mqttOk = false;
        Serial.printf(" failed rc=%d\n", mqtt.state());
    }
}

// ---------------------------------------------------------------------------
// Publish telemetry
// ---------------------------------------------------------------------------
void publishTelemetry() {
    if (!mqttOk) return;
    updateSim();

    JsonDocument doc;

    doc["running"]    = 1;
    doc["fault"]      = 0;
    doc["alarm"]      = 0;
    doc["grid_tied"]  = 1;
    doc["off_grid"]   = 0;
    doc["derating"]   = 0;
    doc["standby"]    = 0;

    doc["freq_hz"]    = sim.freq;
    doc["v_a"]        = sim.v_phase;
    doc["v_b"]        = sim.v_phase - 0.3f;
    doc["v_c"]        = sim.v_phase + 0.2f;
    doc["v_ab"]       = sim.v_phase * 1.732f;
    doc["v_bc"]       = sim.v_phase * 1.732f - 0.5f;
    doc["v_ca"]       = sim.v_phase * 1.732f + 0.3f;
    doc["i_a"]        = sim.p_inv / 3.0f / sim.v_phase * 1000.0f;
    doc["i_b"]        = sim.p_inv / 3.0f / sim.v_phase * 1000.0f - 0.1f;
    doc["i_c"]        = sim.p_inv / 3.0f / sim.v_phase * 1000.0f + 0.1f;
    doc["p_a_kw"]     = sim.p_inv / 3.0f;
    doc["p_b_kw"]     = sim.p_inv / 3.0f - 0.1f;
    doc["p_c_kw"]     = sim.p_inv / 3.0f + 0.1f;
    doc["p_inv_kw"]   = sim.p_inv;
    doc["q_a_kvar"]   = sim.p_inv / 3.0f * 0.1f;
    doc["q_b_kvar"]   = sim.p_inv / 3.0f * 0.1f;
    doc["q_c_kvar"]   = sim.p_inv / 3.0f * 0.1f;
    doc["q_inv_kvar"] = sim.p_inv * 0.1f;
    doc["pf_a"]       = sim.pf;
    doc["pf_b"]       = sim.pf - 0.01f;
    doc["pf_c"]       = sim.pf + 0.01f;
    doc["pf_total"]   = sim.pf;

    doc["dc_voltage_v"] = sim.bms_v;
    doc["dc_current_a"] = sim.p_inv / sim.bms_v * 1000.0f;
    doc["dc_power_kw"]  = sim.p_inv;

    doc["grid_freq_hz"] = sim.freq;
    doc["grid_v_a"]     = sim.v_phase;
    doc["grid_v_b"]     = sim.v_phase - 0.3f;
    doc["grid_v_c"]     = sim.v_phase + 0.2f;
    doc["grid_p_kw"]    = sim.grid_p;

    doc["load_freq_hz"] = sim.freq;
    doc["load_v_a"]     = sim.v_phase;
    doc["load_v_b"]     = sim.v_phase - 0.3f;
    doc["load_v_c"]     = sim.v_phase + 0.2f;
    doc["load_i_a"]     = sim.load_p / 3.0f / sim.v_phase * 1000.0f;
    doc["load_i_b"]     = sim.load_p / 3.0f / sim.v_phase * 1000.0f;
    doc["load_i_c"]     = sim.load_p / 3.0f / sim.v_phase * 1000.0f;
    doc["load_p_a_kw"]  = sim.load_p / 3.0f;
    doc["load_p_b_kw"]  = sim.load_p / 3.0f;
    doc["load_p_c_kw"]  = sim.load_p / 3.0f;
    doc["load_p_kw"]    = sim.load_p;
    doc["load_s_kva"]   = sim.load_p / sim.pf;

    doc["bms_soc_pct"]             = sim.soc;
    doc["bms_soh_pct"]             = 98.0f;
    doc["bms_voltage_v"]           = sim.bms_v;
    doc["bms_current_a"]           = sim.bms_i;
    doc["bms_temp_avg_c"]          = sim.bms_temp_avg;
    doc["bms_temp_cell_max_c"]     = sim.bms_temp_max;
    doc["bms_temp_cell_min_c"]     = sim.bms_temp_min;
    doc["bms_temp_fet_c"]          = sim.bms_temp_fet;
    doc["bms_cell_v_max"]          = sim.bms_cell_v_max;
    doc["bms_cell_v_min"]          = sim.bms_cell_v_min;
    doc["bms_max_charge_a"]        = sim.bms_max_chg_a;
    doc["bms_max_discharge_a"]     = sim.bms_max_dischg_a;
    doc["bms_charge_cutoff_v"]     = sim.bms_chg_cutoff;
    doc["bms_discharge_cutoff_v"]  = sim.bms_dischg_cutoff;
    doc["bms_charging"]            = sim.bms_i > 0 ? 1 : 0;
    doc["bms_discharging"]         = sim.bms_i < 0 ? 1 : 0;
    doc["bms_charge_forbidden"]    = 0;
    doc["bms_discharge_forbidden"] = 0;
    doc["bms_force_charge"]        = 0;
    doc["bms_fault"]               = 0;
    doc["bms_alarm"]               = 0;
    doc["bms_protection"]          = 0;

    char payload[2048];
    serializeJson(doc, payload, sizeof(payload));
    Serial.printf("[MQTT] Payload %d bytes\n", strlen(payload));
    bool ok = mqtt.publish("v1/devices/me/telemetry", payload);
    mqttOk = ok || mqtt.connected();
    Serial.printf("[MQTT] Publish %s — SOC=%d%% P_inv=%.1fkW Grid=%.1fkW\n",
                  ok ? "OK" : "FAIL", (int)sim.soc, sim.p_inv, sim.grid_p);
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[Boot] test_tb starting...");
    randomSeed(esp_random());

    // Display
    pinMode(LCD_BLK, OUTPUT);
    digitalWrite(LCD_BLK, HIGH);
    tft.init(135, 240);
    tft.setRotation(3);
    tft.fillScreen(C_BG);
    tft.setTextColor(C_LABEL); tft.setTextSize(1);
    tft.setCursor(4, 60); tft.print("Connecting WiFi...");
    Serial.println("[Boot] Display ready");

    connectWiFi();
    connectMQTT();

    tft.fillScreen(C_BG);
    Serial.println("[Boot] Ready");
}

uint8_t page = 0;
unsigned long lastDisplay = 0;
unsigned long lastPublish = 0;

void loop() {
    // Reconnect if needed
    if (WiFi.status() != WL_CONNECTED) { connectWiFi(); }
    if (wifiOk && !mqtt.connected())   { connectMQTT(); }
    if (mqttOk) mqtt.loop();

    // Publish every 5s
    if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
        lastPublish = millis();
        publishTelemetry();
    }

    // Cycle display every 3s
    if (millis() - lastDisplay >= DISPLAY_INTERVAL_MS) {
        lastDisplay = millis();
        switch (page % 3) {
            case 0: screenStatus();  Serial.println("[Display] Status");  break;
            case 1: screenPower();   Serial.println("[Display] Power");   break;
            case 2: screenBattery(); Serial.println("[Display] Battery"); break;
        }
        page++;
    }
}
