// =============================================================================
// test_dashboard — Hardware sketch: Modbus polling + BMS (optional) → ThingsBoard
//
// What it does:
//   1. Connects to WiFi and ThingsBoard
//   2. Polls the inverter via Modbus RTU every 10s
//   3. Listens for BMS CAN frames — publishes BMS data if received,
//      silently skips if no CAN converter is connected
//   4. Publishes all telemetry to ThingsBoard
//   5. No control, no RPC, no EMS — pure read and forward
//
// Uses lib/ for protocol logic: modbus_core, inverter_core, bms_core.
// Changing the protocol in lib/ is reflected here automatically.
// Pin and credential configuration is local to this sketch.
//
// Serial at 115200 shows every poll cycle and publish result.
//
// Wiring:
//   MAX485: GPIO17→DI, GPIO16→RO, GPIO5→DE+RE, A/B→inverter RS-485
//   CAN:    GPIO21→TX, GPIO22→RX (optional — set CAN_ENABLED false if not connected)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "modbus_core.h"
#include "inverter_core.h"
#include "inverter_scales.h"
#include "bms_core.h"
#include "bms_scales.h"
#include "driver/twai.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#include "config.h"   // RS485 pins, Modbus ID, CAN config, TB host/port
#define TB_TOKEN         TB_ACCESS_TOKEN
#define MODBUS_ID        MODBUS_DEVICE_ID
#define BMS_ADDR         BMS_CAN_ADDR

// CAN — set to false if no CAN converter connected
#define CAN_ENABLED      false

#define POLL_INTERVAL_MS 10000

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);
BmsData      bmsData = {};

// ---------------------------------------------------------------------------
// Modbus — uses modbus_core from lib/ for framing and parsing
// ---------------------------------------------------------------------------
bool modbusRead(uint16_t startReg, uint16_t count, int16_t* out) {
    uint8_t frame[8];
    modbus_build_read(frame, MODBUS_ID, startReg, count);

    digitalWrite(RS485_DE_RE_PIN, HIGH);
    delayMicroseconds(50);
    Serial2.write(frame, 8);
    Serial2.flush();
    delayMicroseconds(50);
    digitalWrite(RS485_DE_RE_PIN, LOW);

    uint8_t rxBuf[256];
    uint32_t t = millis();
    uint8_t idx = 0;
    while ((millis() - t) < 50 && idx == 0)
        if (Serial2.available()) rxBuf[idx++] = Serial2.read();
    if (idx == 0) return false;
    t = millis();
    while ((millis() - t) < 20)
        if (Serial2.available() && idx < sizeof(rxBuf)) rxBuf[idx++] = Serial2.read();

    return modbus_parse_read(rxBuf, idx, count, out);
}

// ---------------------------------------------------------------------------
// WiFi / MQTT
// ---------------------------------------------------------------------------
void connectWiFi() {
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectMQTT() {
    mqtt.setServer(TB_HOST, TB_PORT);
    mqtt.setBufferSize(2048);
    String clientId = "ESP32_dashboard_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    while (!mqtt.connected()) {
        Serial.print("[MQTT] Connecting...");
        if (mqtt.connect(clientId.c_str(), TB_TOKEN, nullptr))
            Serial.println(" OK");
        else {
            Serial.printf(" failed rc=%d, retrying in 5s\n", mqtt.state());
            delay(5000);
        }
    }
}

// ---------------------------------------------------------------------------
// Poll inverter — uses inverter_core from lib/ for register parsing
// ---------------------------------------------------------------------------
void pollInverter(JsonDocument& doc) {
    int16_t raw[26];

    if (modbusRead(32, 1, raw)) {
        StatusData s; inverter_parse_status(raw, s);
        doc["fault"]     = s.fault;
        doc["alarm"]     = s.alarm;
        doc["running"]   = s.running;
        doc["grid_tied"] = s.grid_tied;
        doc["off_grid"]  = s.off_grid;
        doc["standby"]   = s.standby;
        Serial.printf("[Modbus] Status: running=%d grid=%d fault=%d\n",
                      s.running, s.grid_tied, s.fault);
    } else Serial.println("[Modbus] FAIL: reg 32 (status)");

    if (modbusRead(100, 26, raw)) {
        AcData ac; inverter_parse_ac(raw, ac);
        doc["freq_hz"]    = ac.freq_hz;
        doc["v_a"]        = ac.v_a;
        doc["v_b"]        = ac.v_b;
        doc["v_c"]        = ac.v_c;
        doc["i_a"]        = ac.i_a;
        doc["i_b"]        = ac.i_b;
        doc["i_c"]        = ac.i_c;
        doc["p_a_kw"]     = ac.p_a;
        doc["p_b_kw"]     = ac.p_b;
        doc["p_c_kw"]     = ac.p_c;
        doc["p_inv_kw"]   = ac.p_inv;
        doc["q_inv_kvar"] = ac.q_inv;
        doc["pf_total"]   = ac.pf_total;
        Serial.printf("[Modbus] AC: %.1fV %.2fkW PF=%.2f\n",
                      ac.v_a, ac.p_inv, ac.pf_total);
    } else Serial.println("[Modbus] FAIL: reg 100 (AC)");

    if (modbusRead(141, 3, raw)) {
        DcData dc; inverter_parse_dc(raw, dc);
        doc["dc_power_kw"]  = dc.power_kw;
        doc["dc_voltage_v"] = dc.voltage_v;
        doc["dc_current_a"] = dc.current_a;
        Serial.printf("[Modbus] DC: %.1fV %.2fkW\n", dc.voltage_v, dc.power_kw);
    } else Serial.println("[Modbus] FAIL: reg 141 (DC)");

    int16_t grid_p_raw = 0;
    if (modbusRead(170, 10, raw)) {
        modbusRead(192, 1, &grid_p_raw);
        GridData g; inverter_parse_grid(raw, grid_p_raw, g);
        doc["grid_freq_hz"] = g.freq_hz;
        doc["grid_v_a"]     = g.v_a;
        doc["grid_v_b"]     = g.v_b;
        doc["grid_v_c"]     = g.v_c;
        doc["grid_p_kw"]    = g.p_kw;
        Serial.printf("[Modbus] Grid: %.1fHz %.2fkW\n", g.freq_hz, g.p_kw);
    } else Serial.println("[Modbus] FAIL: reg 170 (grid)");

    if (modbusRead(200, 14, raw)) {
        LoadData l; inverter_parse_load(raw, l);
        doc["load_p_kw"]  = l.p_total;
        doc["load_s_kva"] = l.s_total;
        Serial.printf("[Modbus] Load: %.2fkW\n", l.p_total);
    } else Serial.println("[Modbus] FAIL: reg 200 (load)");
}

// ---------------------------------------------------------------------------
// Poll BMS CAN — uses bms_core from lib/ for frame decoding
// ---------------------------------------------------------------------------
void pollBMS(JsonDocument& doc) {
    if (!CAN_ENABLED) return;

    twai_message_t msg;
    int count = 0;
    while (twai_receive(&msg, pdMS_TO_TICKS(10)) == ESP_OK) {
        bms_decode(bmsData, BMS_ADDR, msg.identifier, msg.data);
        count++;
    }

    if (!bmsData.valid) {
        Serial.println("[BMS] No data yet");
        return;
    }

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
    delay(500);
    Serial.println("\n[Boot] test_dashboard starting...");

    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW);
    Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    Serial.println("[Boot] RS-485 ready");

    if (CAN_ENABLED) {
        twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(CAN_TX_PIN, CAN_RX_PIN, TWAI_MODE_NORMAL);
        twai_timing_config_t  t = CAN_SPEED;
        twai_filter_config_t  f = TWAI_FILTER_CONFIG_ACCEPT_ALL();
        if (twai_driver_install(&g, &t, &f) == ESP_OK && twai_start() == ESP_OK)
            Serial.println("[Boot] CAN ready");
        else
            Serial.println("[Boot] CAN init failed — BMS data will be skipped");
    }

    connectWiFi();
    connectMQTT();

    Serial.println("[Boot] Ready — polling every 10s");
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (!mqtt.connected())             connectMQTT();
    mqtt.loop();

    static unsigned long lastPoll = 0;
    if (millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();

        JsonDocument doc;
        pollInverter(doc);
        pollBMS(doc);

        char payload[2048];
        serializeJson(doc, payload, sizeof(payload));
        Serial.printf("[MQTT] Payload: %d bytes\n", strlen(payload));
        bool ok = mqtt.publish("v1/devices/me/telemetry", payload);
        Serial.printf("[MQTT] Publish %s\n", ok ? "OK" : "FAIL");
    }
}
