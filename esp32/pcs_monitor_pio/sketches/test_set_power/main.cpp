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
#include "inverter.h"

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
// Apply setpoint to inverter
// ---------------------------------------------------------------------------
void applySetPower(float kw) {
    // Clamp to testbed range — change to -100/100 for production
    kw = constrain(kw, -2.0f, 2.0f);
    inverterSetPower(kw);
    Serial.printf("[SetPower] %.1f kW applied\n", kw);
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

    // Use pollModbus for all inverter data
    pollModbus(doc);
    doc["set_power_requested"] = setPowerRequested;

    char payload[2048];
    serializeJson(doc, payload, sizeof(payload));
    bool ok = mqtt.publish(TOPIC_TELEMETRY, payload);
    Serial.printf("[MQTT] Publish %s (%d bytes)\n", ok ? "OK" : "FAIL", strlen(payload));
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Boot] test_set_power starting...");

    Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
    inverterInit(Serial2, RS485_DE_RE_PIN);

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
