// =============================================================================
// test_set_power — Shared attribute setPower → Modbus reg 135 → telemetry
//
// What it does:
//   1. Connects WiFi and ThingsBoard
//   2. On boot: requests current value of shared attribute "set_power" and applies it
//   3. On change: receives notification when "set_power" changes and applies it
//   4. Every 5s: polls inverter reg 135 (active setpoint), p_inv_kw, dc_power_kw,
//      dc_current_a and publishes as telemetry
//
// ThingsBoard keys:
//   Shared attribute (input):  set_power          [-100..+100 kW, 0.1 kW precision]
//   Telemetry (output):        set_power_requested [kW, what was received from TB]
//                              set_power_active    [kW, read back from inverter reg 135]
//                              p_inv_kw            [kW, AC output power]
//                              dc_power_kw         [kW, DC power]
//                              dc_current_a        [A,  DC current]
//
// Wiring: MAX485 GPIO17→DI, GPIO16→RO, GPIO5→DE+RE, A/B→inverter RS-485
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "credentials.h"   // WIFI_SSID, WIFI_PASSWORD, TB_ACCESS_TOKEN
#include "modbus_core.h"
#include "inverter_core.h"
#include "inverter_scales.h"

// ---------------------------------------------------------------------------
// ThingsBoard
// ---------------------------------------------------------------------------
#define TB_TOKEN          TB_ACCESS_TOKEN

// ThingsBoard MQTT topics
#define TOPIC_TELEMETRY          "v1/devices/me/telemetry"
#define TOPIC_ATTRIBUTES_SUB     "v1/devices/me/attributes"
#define TOPIC_ATTR_REQUEST       "v1/devices/me/attributes/request/1"
#define TOPIC_ATTR_RESPONSE      "v1/devices/me/attributes/response/1"

#define POLL_INTERVAL_MS  5000

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

float setPowerRequested = 0.0f;   // last value received from ThingsBoard
bool  newSetpoint       = false;  // flag: apply setpoint on next loop

// ---------------------------------------------------------------------------
// Modbus helpers
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
// Apply setpoint to inverter
// ---------------------------------------------------------------------------
void applySetPower(float kw) {
    // Clamp to testbed range — change to -100/100 for production
    kw = constrain(kw, -2.0f, 2.0f);

    int16_t raw = (int16_t)(kw / SCALE_SET_POWER_KW);
    bool ok = modbusWrite(REG_SET_POWER, raw);

    Serial.printf("[SetPower] %.1f kW → raw=%d → %s\n",
                  kw, raw, ok ? "OK" : "FAIL");
}

// ---------------------------------------------------------------------------
// MQTT callback — receives shared attribute changes and attribute responses
// ---------------------------------------------------------------------------
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
    JsonDocument doc;
    deserializeJson(doc, payload, length);

    // Both attribute notifications and responses use the same key
    if (doc["set_power"].is<float>()) {
        float val = doc["set_power"].as<float>();
        Serial.printf("[TB] set_power = %.1f kW\n", val);
        setPowerRequested = val;
        newSetpoint = true;
    }
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
    mqtt.setBufferSize(512);
    mqtt.setCallback(onMqttMessage);
    String clientId = "ESP32_setpower_" + String((uint32_t)ESP.getEfuseMac(), HEX);

    while (!mqtt.connected()) {
        Serial.print("[MQTT] Connecting...");
        if (mqtt.connect(clientId.c_str(), TB_TOKEN, nullptr)) {
            Serial.println(" OK");
            // Subscribe to shared attribute changes
            mqtt.subscribe(TOPIC_ATTRIBUTES_SUB);
            // Subscribe to attribute request responses
            mqtt.subscribe(TOPIC_ATTR_RESPONSE);
            // Request current value of set_power on boot
            mqtt.publish(TOPIC_ATTR_REQUEST,
                         "{\"sharedKeys\":\"set_power\"}");
            Serial.println("[MQTT] Requested current set_power from TB");
        } else {
            Serial.printf(" failed rc=%d, retrying in 5s\n", mqtt.state());
            delay(5000);
        }
    }
}

// ---------------------------------------------------------------------------
// Poll inverter and publish telemetry
// ---------------------------------------------------------------------------
void pollAndPublish() {
    JsonDocument doc;
    bool anyOk = false;

    // Read back reg 135 (active setpoint)
    int16_t raw135 = 0;
    if (modbusRead(REG_SET_POWER, 1, &raw135)) {
        float active = raw135 * SCALE_SET_POWER_KW;
        doc["set_power_requested"] = setPowerRequested;
        doc["set_power_active"]    = active;
        Serial.printf("[Poll] set_power: requested=%.1f active=%.1f kW\n",
                      setPowerRequested, active);
        anyOk = true;
    } else Serial.println("[Poll] FAIL: reg 135 (set_power)");

    // Read DC (reg 141–143)
    int16_t dc_raw[3];
    if (modbusRead(REG_DC_START, REG_DC_COUNT, dc_raw)) {
        DcData dc; inverter_parse_dc(dc_raw, dc);
        doc["dc_power_kw"]  = dc.power_kw;
        doc["dc_current_a"] = dc.current_a;
        Serial.printf("[Poll] DC: %.2fkW  %.1fA\n", dc.power_kw, dc.current_a);
        anyOk = true;
    } else Serial.println("[Poll] FAIL: reg 141 (DC)");

    // Read AC total power (reg 100–125, only need p_inv)
    int16_t ac_raw[26];
    if (modbusRead(REG_AC_START, REG_AC_COUNT, ac_raw)) {
        AcData ac; inverter_parse_ac(ac_raw, ac);
        doc["p_inv_kw"] = ac.p_inv;
        Serial.printf("[Poll] AC p_inv: %.2fkW\n", ac.p_inv);
        anyOk = true;
    } else Serial.println("[Poll] FAIL: reg 100 (AC)");

    // Read Grid (reg 170–179 + 192)
    int16_t grid_raw[10];
    int16_t grid_p_raw = 0;
    if (modbusRead(REG_GRID_START, REG_GRID_COUNT, grid_raw) &&
        modbusRead(REG_GRID_POWER, 1, &grid_p_raw)) {
        GridData g; inverter_parse_grid(grid_raw, grid_p_raw, g);
        doc["grid_p_kw"] = g.p_kw;
        Serial.printf("[Poll] Grid: %.2fkW\n", g.p_kw);
        anyOk = true;
    } else Serial.println("[Poll] FAIL: reg 170/192 (grid)");

    // Read Load (reg 200–213, V3.0+)
    int16_t load_raw[14];
    if (modbusRead(REG_LOAD_START, REG_LOAD_COUNT, load_raw)) {
        LoadData l; inverter_parse_load(load_raw, l);
        doc["load_p_kw"] = l.p_total;
        Serial.printf("[Poll] Load: %.2fkW\n", l.p_total);
        anyOk = true;
    } else Serial.println("[Poll] FAIL: reg 200 (load)");

    if (anyOk) {
        char payload[512];
        serializeJson(doc, payload, sizeof(payload));
        bool ok = mqtt.publish(TOPIC_TELEMETRY, payload);
        Serial.printf("[MQTT] Publish %s (%d bytes)\n", ok ? "OK" : "FAIL", strlen(payload));
    }
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Boot] test_set_power starting...");

    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW);
    RS485_SERIAL.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    Serial.println("[Boot] RS-485 ready");

    Serial.println("[Boot] Running inverter init sequence...");
    inverter_run_init(modbusWrite, modbusRead);
    Serial.println("[Boot] Inverter init done.");

    connectWiFi();
    connectMQTT();

    Serial.println("[Boot] Ready — move the knob in ThingsBoard to set power");
    Serial.println("[Boot] Positive = discharge battery, Negative = charge battery");
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (!mqtt.connected())             connectMQTT();
    mqtt.loop();

    // Apply new setpoint as soon as it arrives
    if (newSetpoint) {
        newSetpoint = false;
        applySetPower(setPowerRequested);
    }

    // Poll and publish every 5s
    static unsigned long lastPoll = 0;
    if (millis() - lastPoll >= POLL_INTERVAL_MS) {
        lastPoll = millis();
        pollAndPublish();
    }
}
