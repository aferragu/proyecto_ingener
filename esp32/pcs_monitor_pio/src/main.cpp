// =============================================================================
// PCS Inverter Monitor — ESP32
//
// Modbus RTU RS-485 → inversor SinoSoar SP6030 (protocolo V3.0)
// Modbus RTU RS-485 → BMS LWS (protocolo V1.36)
// WiFi MQTT         → ThingsBoard thingsboard.cloud
//
// Librerías:
//   - PubSubClient  (Nick O'Leary)
//   - ArduinoJson   (Benoit Blanchon)
//   - ModbusMaster  (Doc Walker)
//
// Conexiones MAX485:  GPIO17→DI, GPIO16→RO, GPIO5→DE+RE
// LED status:         GPIO2 (activo en LOW)
//
// RPCs: powerOn, shutdown, setPower {"value": X}
// =============================================================================

#include <Arduino.h>
#include "config.h"
#include "inverter.h"
#include "bms.h"
#include "mqtt.h"
#include "ems.h"
#include <WiFi.h>

StaticJsonDocument<2048> telemetry;

unsigned long lastModbusMs  = 0;
unsigned long lastBmsMs     = 0;
unsigned long lastPublishMs = 0;
unsigned long lastVerifyMs  = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("\n[Boot] PCS Monitor arrancando...");

    pinMode(LED_PIN, OUTPUT);
    LED_OFF();

    // RS-485 — shared bus for inverter and BMS
    Serial2.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

    inverterInit(Serial2, RS485_DE_RE_PIN);
    bmsInit(Serial2, RS485_DE_RE_PIN);

    connectWiFi();
    connectMQTT();
    readFirmwareVersion(mqttClient);

    pollModbus(telemetry);
    pollBMS(telemetry);
    publishTelemetry(telemetry);

    lastModbusMs  = millis();
    lastBmsMs     = millis();
    lastPublishMs = millis();
    lastVerifyMs  = millis();

    Serial.println("[Boot] Listo.");
}

void loop() {
    if (!mqttClient.connected()) connectMQTT();
    mqttClient.loop();

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WiFi] Reconectando...");
        connectWiFi();
    }

    updateLed();

    unsigned long now = millis();

    if (now - lastVerifyMs >= VERIFY_INIT_MS) {
        lastVerifyMs = now;
        verifyAndReinit();
    }

    if (now - lastModbusMs >= POLL_MODBUS_MS) {
        lastModbusMs = now;
        pollModbus(telemetry);
        emsUpdate(telemetry);
    }

    if (now - lastBmsMs >= POLL_BMS_MS) {
        lastBmsMs = now;
        pollBMS(telemetry);
    }

    if (now - lastPublishMs >= PUBLISH_MS) {
        lastPublishMs = now;
        publishTelemetry(telemetry);
    }
}
