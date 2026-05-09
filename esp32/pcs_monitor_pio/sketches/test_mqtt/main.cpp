// =============================================================================
// test_mqtt — Hardware sketch: WiFi + ThingsBoard MQTT
//
// What it does:
//   1. Connects to WiFi
//   2. Connects to ThingsBoard via MQTT
//   3. Sends a fixed dummy telemetry payload every 5 seconds
//   4. Listens for RPC requests and prints them to Serial
//   5. Responds to any RPC with {"result":"ok"}
//
// Watch Serial at 115200. You should see telemetry arriving in ThingsBoard
// and can trigger RPCs from the device's RPC tab to verify the round-trip.
//
// Wiring: none — WiFi only.
// Credentials: set WIFI_SSID, WIFI_PASSWORD, TB_ACCESS_TOKEN below.
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#include "config.h"   // TB_HOST, TB_PORT
#define TB_TOKEN        TB_ACCESS_TOKEN

#define PUBLISH_INTERVAL_MS 5000

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);

// ---------------------------------------------------------------------------
// RPC callback
// ---------------------------------------------------------------------------
void onRpc(char* topic, byte* payload, unsigned int length) {
    String topicStr(topic);
    String requestId = topicStr.substring(topicStr.lastIndexOf('/') + 1);

    JsonDocument req;
    deserializeJson(req, payload, length);

    Serial.printf("[RPC] id=%s method=%s\n",
                  requestId.c_str(),
                  req["method"].as<const char*>());

    if (req["params"].is<JsonObject>())  {
        String params;
        serializeJson(req["params"], params);
        Serial.printf("[RPC] params=%s\n", params.c_str());
    }

    String responseTopic = "v1/devices/me/rpc/response/" + requestId;
    mqtt.publish(responseTopic.c_str(), "{\"result\":\"ok\"}");
    Serial.println("[RPC] responded ok");
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
void connectWiFi() {
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500); Serial.print(".");
    }
    Serial.printf("\n[WiFi] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
}

// ---------------------------------------------------------------------------
// MQTT
// ---------------------------------------------------------------------------
void connectMQTT() {
    mqtt.setServer(TB_HOST, TB_PORT);
    mqtt.setBufferSize(512);
    mqtt.setCallback(onRpc);

    String clientId = "ESP32_test_mqtt_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    while (!mqtt.connected()) {
        Serial.print("[MQTT] Connecting...");
        if (mqtt.connect(clientId.c_str(), TB_TOKEN, nullptr)) {
            Serial.println(" OK");
            mqtt.subscribe("v1/devices/me/rpc/request/+");
            Serial.println("[MQTT] Subscribed to RPC requests");
        } else {
            Serial.printf(" failed rc=%d, retrying in 5s\n", mqtt.state());
            delay(5000);
        }
    }
}

// ---------------------------------------------------------------------------
// Publish dummy telemetry
// ---------------------------------------------------------------------------
void publishTelemetry() {
    JsonDocument doc;
    doc["test_counter"]  = millis() / 1000;
    doc["dummy_voltage"] = 230.5f;
    doc["dummy_power"]   = 12.3f;
    doc["dummy_soc"]     = 75;
    doc["rssi"]          = WiFi.RSSI();

    char payload[256];
    serializeJson(doc, payload, sizeof(payload));

    bool ok = mqtt.publish("v1/devices/me/telemetry", payload);
    Serial.printf("[MQTT] Publish %s: %s\n", ok ? "OK" : "FAIL", payload);
}

// ---------------------------------------------------------------------------
// Setup / Loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Boot] test_mqtt starting...");

    connectWiFi();
    connectMQTT();

    Serial.println("[Boot] Ready — publishing every 5s, waiting for RPCs");
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) connectWiFi();
    if (!mqtt.connected())             connectMQTT();
    mqtt.loop();

    static unsigned long lastPublish = 0;
    if (millis() - lastPublish >= PUBLISH_INTERVAL_MS) {
        lastPublish = millis();
        publishTelemetry();
    }
}
