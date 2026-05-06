#include "mqtt.h"
#include "config.h"
#include "modbus.h"
#include "inverter_scales.h"
#include <WiFi.h>

WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);

void updateLed() {
    if (WiFi.status() == WL_CONNECTED && mqttClient.connected())
        LED_ON();
    else
        LED_OFF();
}

void connectWiFi() {
    Serial.printf("[WiFi] Conectando a %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.printf("\n[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
}

void onRpcMessage(char* topic, byte* payload, unsigned int length) {
    String topicStr(topic);
    String requestId = topicStr.substring(topicStr.lastIndexOf('/') + 1);
    String responseTopic = "v1/devices/me/rpc/response/" + requestId;

    StaticJsonDocument<256> req;
    deserializeJson(req, payload, length);
    const char* method = req["method"];

    bool success = false;
    String response = "{\"result\":\"ok\"}";
    Serial.printf("[RPC] Método: %s\n", method);

    if (strcmp(method, "powerOn") == 0) {
        success = writeRegister(REG_POWER_ON, 1);
        Serial.printf("[RPC] powerOn: %s\n", success ? "OK" : "FAIL");

    } else if (strcmp(method, "shutdown") == 0) {
        success = writeRegister(REG_SHUTDOWN, 1);
        Serial.printf("[RPC] shutdown: %s\n", success ? "OK" : "FAIL");

    } else if (strcmp(method, "setPower") == 0) {
        float kw = req["params"]["value"] | 0.0f;
        kw = constrain(kw, -100.0f, 100.0f);
        int16_t raw = (int16_t)(kw / SCALE_SET_POWER_KW);
        success = writeRegister(REG_SET_POWER, raw);
        Serial.printf("[RPC] setPower %.1f kW (raw=%d): %s\n", kw, raw, success ? "OK" : "FAIL");
        if (success)
            response = "{\"result\":\"ok\",\"value\":" + String(kw, 1) + "}";

    } else {
        Serial.printf("[RPC] Método desconocido: %s\n", method);
        mqttClient.publish(responseTopic.c_str(),
            "{\"result\":\"error\",\"message\":\"unknown method\"}");
        return;
    }

    if (!success)
        response = "{\"result\":\"error\",\"message\":\"modbus write failed\"}";

    mqttClient.publish(responseTopic.c_str(), response.c_str());
}

void connectMQTT() {
    static bool initialized = false;
    if (!initialized) {
        mqttClient.setServer(TB_HOST, TB_PORT);
        mqttClient.setBufferSize(2048);
        mqttClient.setCallback(onRpcMessage);
        initialized = true;
    }
    String clientId = "ESP32_PCS_" + String((uint32_t)ESP.getEfuseMac(), HEX);
    while (!mqttClient.connected()) {
        Serial.print("[MQTT] Conectando...");
        if (mqttClient.connect(clientId.c_str(), TB_ACCESS_TOKEN, nullptr)) {
            Serial.println(" OK");
            mqttClient.subscribe("v1/devices/me/rpc/request/+");
            Serial.println("[MQTT] Suscrito a RPC requests");
        } else {
            Serial.printf(" fallo rc=%d, reintentando en 5s\n", mqttClient.state());
            delay(5000);
        }
    }
}

void publishTelemetry(JsonDocument& telemetry) {
    if (!mqttClient.connected()) connectMQTT();
    char payload[2048];
    serializeJson(telemetry, payload, sizeof(payload));
    bool ok = mqttClient.publish("v1/devices/me/telemetry", payload);
    Serial.printf("[MQTT] Publish %s (%d bytes)\n", ok ? "OK" : "FAIL", strlen(payload));
}
