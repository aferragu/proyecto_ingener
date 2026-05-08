// =============================================================================
// test_dashboard — Real hardware telemetry → ThingsBoard + ST7789 display
//
// Same structure as test_tb but reads from real hardware instead of simulation.
// Use flags to enable/disable each source independently.
//
// Flags:
//   MODBUS_ENABLED  — read inverter via RS-485. If 0, inverter keys not published.
//   CAN_ENABLED     — read BMS via CAN.       If 0, BMS keys not published.
//
// Display shows same 3 screens as test_tb with real values.
// Status screen shows red/green dots per source so you know what's working.
//
// Wiring:
//   MAX485: GPIO17→DI, GPIO16→RO, GPIO5→DE+RE, A/B→inverter RS-485
//   CAN:    GPIO21→TX, GPIO22→RX, CAN_H/CAN_L→BMS
//
// Pins and addresses from config.h. Credentials from credentials.h.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include "config.h"
#include "credentials.h"   // WIFI_SSID, WIFI_PASSWORD, TB_ACCESS_TOKEN
#include "modbus_core.h"
#include "inverter_core.h"
#include "inverter_scales.h"
#include "bms_core.h"
#include "bms_scales.h"
#include "driver/twai.h"

// ---------------------------------------------------------------------------
// Flags — set to 0 to disable a source
// ---------------------------------------------------------------------------
#define MODBUS_ENABLED   1
#define CAN_ENABLED      0

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
BmsData         bmsData = {};

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
    snprintf(buf, sizeof(buf), "%d%%", bmsData.soc_pct);
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
        snprintf(buf, sizeof(buf), "SOC: %d%%", bmsData.soc_pct);
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
        snprintf(buf, sizeof(buf), "%.1f C", bmsData.temperature_c);
        drawRow(5, "Temp", buf, bmsData.temperature_c > 35 ? C_WARN : C_VALUE);
    } else {
        tft.setTextColor(C_FAULT); tft.setTextSize(2);
        tft.setCursor(4, 50); tft.print("BMS");
        tft.setCursor(4, 74); tft.print("not connected");
    }
}

// ---------------------------------------------------------------------------
// Modbus read
// ---------------------------------------------------------------------------
bool modbusRead(uint16_t startReg, uint16_t count, int16_t* out) {
    uint8_t frame[8];
    modbus_build_read(frame, MODBUS_DEVICE_ID, startReg, count);

    digitalWrite(RS485_DE_RE_PIN, HIGH);
    delayMicroseconds(50);
    RS485_SERIAL.write(frame, 8);
    RS485_SERIAL.flush();
    delayMicroseconds(50);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    uint8_t rxBuf[256];
    uint32_t t = millis();
    uint8_t idx = 0;
    while ((millis() - t) < 50 && idx == 0)
        if (RS485_SERIAL.available()) rxBuf[idx++] = RS485_SERIAL.read();
    if (idx == 0) return false;
    t = millis();
    while ((millis() - t) < 20)
        if (RS485_SERIAL.available() && idx < (uint16_t)sizeof(rxBuf)) rxBuf[idx++] = RS485_SERIAL.read();

    return modbus_parse_read(rxBuf, idx, count, out);
}

bool modbusWrite(uint16_t reg, int16_t value) {
    uint8_t frame[8];
    modbus_build_write(frame, MODBUS_DEVICE_ID, reg, value);

    digitalWrite(RS485_DE_RE_PIN, HIGH);
    delayMicroseconds(50);
    RS485_SERIAL.write(frame, 8);
    RS485_SERIAL.flush();
    delayMicroseconds(50);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    uint8_t rxBuf[8];
    uint32_t t = millis();
    uint8_t idx = 0;
    while ((millis() - t) < 50 && idx == 0)
        if (RS485_SERIAL.available()) rxBuf[idx++] = RS485_SERIAL.read();
    if (idx == 0) return false;
    t = millis();
    while ((millis() - t) < 20)
        if (RS485_SERIAL.available() && idx < 8) rxBuf[idx++] = RS485_SERIAL.read();

    return modbus_parse_write(rxBuf, idx);
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
void pollInverter(JsonDocument& doc) {
    if (!MODBUS_ENABLED) return;

    int16_t raw[26];
    bool anyOk = false;

    if (modbusRead(REG_STATUS, REG_STATUS_COUNT, raw)) {
        anyOk = true;
        StatusData s; inverter_parse_status(raw, s);
        doc["fault"]     = s.fault;
        doc["alarm"]     = s.alarm;
        doc["running"]   = s.running;
        doc["grid_tied"] = s.grid_tied;
        doc["off_grid"]  = s.off_grid;
        doc["standby"]   = s.standby;
        Serial.printf("[Modbus] Status: running=%d grid=%d fault=%d\n",
                      s.running, s.grid_tied, s.fault);
    } else Serial.println("[Modbus] FAIL: reg 32");

    if (modbusRead(REG_AC_START, REG_AC_COUNT, raw)) {
        anyOk = true;
        AcData ac; inverter_parse_ac(raw, ac);
        doc["freq_hz"]    = ac.freq_hz;
        doc["v_a"]        = ac.v_a;  doc["v_b"] = ac.v_b;  doc["v_c"] = ac.v_c;
        doc["v_ab"]       = ac.v_ab; doc["v_bc"]= ac.v_bc; doc["v_ca"]= ac.v_ca;
        doc["i_a"]        = ac.i_a;  doc["i_b"] = ac.i_b;  doc["i_c"] = ac.i_c;
        doc["p_a_kw"]     = ac.p_a;  doc["p_b_kw"]= ac.p_b; doc["p_c_kw"]= ac.p_c;
        doc["p_inv_kw"]   = ac.p_inv;
        doc["q_inv_kvar"] = ac.q_inv;
        doc["pf_total"]   = ac.pf_total;
        disp_p_inv = ac.p_inv; disp_freq = ac.freq_hz;
        disp_v_a   = ac.v_a;  disp_pf   = ac.pf_total;
        Serial.printf("[Modbus] AC: %.1fV %.2fkW PF=%.2f\n", ac.v_a, ac.p_inv, ac.pf_total);
    } else Serial.println("[Modbus] FAIL: reg 100");

    if (modbusRead(REG_DC_START, REG_DC_COUNT, raw)) {
        anyOk = true;
        DcData dc; inverter_parse_dc(raw, dc);
        doc["dc_power_kw"]  = dc.power_kw;
        doc["dc_voltage_v"] = dc.voltage_v;
        doc["dc_current_a"] = dc.current_a;
    } else Serial.println("[Modbus] FAIL: reg 141");

    int16_t grid_p_raw = 0;
    if (modbusRead(REG_GRID_START, REG_GRID_COUNT, raw) &&
        modbusRead(REG_GRID_POWER, 1, &grid_p_raw)) {
        anyOk = true;
        GridData g; inverter_parse_grid(raw, grid_p_raw, g);
        doc["grid_freq_hz"] = g.freq_hz;
        doc["grid_v_a"]     = g.v_a; doc["grid_v_b"]= g.v_b; doc["grid_v_c"]= g.v_c;
        doc["grid_p_kw"]    = g.p_kw;
        disp_grid_p = g.p_kw;
        Serial.printf("[Modbus] Grid: %.1fHz %.2fkW\n", g.freq_hz, g.p_kw);
    } else Serial.println("[Modbus] FAIL: reg 170/192");

    if (modbusRead(REG_LOAD_START, REG_LOAD_COUNT, raw)) {
        anyOk = true;
        LoadData l; inverter_parse_load(raw, l);
        doc["load_p_kw"]  = l.p_total;
        doc["load_s_kva"] = l.s_total;
        disp_load_p = l.p_total;
        Serial.printf("[Modbus] Load: %.2fkW\n", l.p_total);
    } else Serial.println("[Modbus] FAIL: reg 200");

    modbusOk = anyOk;
}

// ---------------------------------------------------------------------------
// Poll BMS
// ---------------------------------------------------------------------------
void pollBMS(JsonDocument& doc) {
    if (!CAN_ENABLED) return;

    twai_message_t msg;
    int count = 0;
    while (twai_receive(&msg, pdMS_TO_TICKS(10)) == ESP_OK) {
        bms_decode(bmsData, BMS_CAN_ADDR, msg.identifier, msg.data);
        count++;
    }

    canOk = bmsData.valid;
    if (!canOk) { Serial.println("[BMS] No data"); return; }

    doc["bms_soc_pct"]            = bmsData.soc_pct;
    doc["bms_soh_pct"]            = bmsData.soh_pct;
    doc["bms_voltage_v"]          = bmsData.voltage_v;
    doc["bms_current_a"]          = bmsData.current_a;
    doc["bms_temperature_c"]      = bmsData.temperature_c;
    doc["bms_max_charge_a"]       = bmsData.max_charge_a;
    doc["bms_max_discharge_a"]    = bmsData.max_discharge_a;
    doc["bms_charge_forbidden"]   = bmsData.charge_forbidden   ? 1 : 0;
    doc["bms_discharge_forbidden"]= bmsData.discharge_forbidden ? 1 : 0;
    doc["bms_fault"]              = bmsData.fault;
    doc["bms_alarm"]              = bmsData.alarm;
    doc["bms_status"]             = bmsData.status;
    Serial.printf("[BMS] SOC=%d%% V=%.1fV I=%.1fA T=%.1f°C (%d frames)\n",
                  bmsData.soc_pct, bmsData.voltage_v,
                  bmsData.current_a, bmsData.temperature_c, count);
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[Boot] test_dashboard starting...");
    Serial.printf("[Boot] Modbus=%d  CAN=%d\n", MODBUS_ENABLED, CAN_ENABLED);

    // Display
    pinMode(LCD_BLK, OUTPUT); digitalWrite(LCD_BLK, HIGH);
    tft.init(135, 240); tft.setRotation(3); tft.fillScreen(C_BG);
    tft.setTextColor(C_LABEL); tft.setTextSize(1);
    tft.setCursor(4, 60); tft.print("Connecting...");

    // RS-485
    if (MODBUS_ENABLED) {
        pinMode(RS485_DE_RE_PIN, OUTPUT);
        digitalWrite(RS485_DE_RE_PIN, LOW);
        RS485_SERIAL.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
        Serial.println("[Boot] RS-485 ready");
        Serial.println("[Boot] Running inverter init...");
        inverter_run_init(modbusWrite, modbusRead);
        Serial.println("[Boot] Inverter init done");
    }

    // CAN
    if (CAN_ENABLED) {
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
        twai_timing_config_t  t = CAN_SPEED;
        twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
        if (twai_driver_install(&g, &t, &f) == ESP_OK && twai_start() == ESP_OK)
            Serial.println("[Boot] CAN ready");
        else
            Serial.println("[Boot] CAN init failed");
    }

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
        pollInverter(doc);
        pollBMS(doc);

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
