// =============================================================================
// PCS Inverter Monitor — ESP32
//
// Modbus RTU RS-485 → inversor SinoSoar SP6030 (protocolo V3.0)
// CAN bus (TWAI)    → BMS Pylontech high voltage (protocolo V1.24)
// WiFi MQTT         → ThingsBoard thingsboard.cloud
//
// Librerías (Arduino Library Manager):
//   - PubSubClient  (Nick O'Leary)
//   - ArduinoJson   (Benoit Blanchon)
//
// Conexiones MAX485:  GPIO17→DI, GPIO16→RO, GPIO4→DE+RE
// Conexiones CAN:     GPIO21→TX, GPIO22→RX
// LED status:         GPIO2 (activo en LOW)
//
// RPCs: powerOn, shutdown, setPower {"value": X}
// =============================================================================

#include "config.h"
#include "modbus.h"
#include "inverter.h"
#include "bms.h"
#include "mqtt.h"
#include "ems.h"
#include <WiFi.h>

StaticJsonDocument<2048> telemetry;

unsigned long lastModbusMs  = 0;
unsigned long lastCanMs     = 0;
unsigned long lastPublishMs = 0;
unsigned long lastVerifyMs  = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("\n[Boot] PCS Monitor arrancando...");

    pinMode(LED_PIN, OUTPUT);
    LED_OFF();

    pinMode(RS485_DE_RE_PIN, OUTPUT);
    digitalWrite(RS485_DE_RE_PIN, LOW);
    RS485_SERIAL.begin(RS485_BAUD, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);

    initCAN();
    initBMS();
    connectWiFi();
    connectMQTT();
    readFirmwareVersion(mqttClient);
    inverterInit();

    pollModbus(telemetry);
    pollCAN(telemetry);
    publishTelemetry(telemetry);

    lastModbusMs  = millis();
    lastCanMs     = millis();
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

    if (now - lastCanMs >= POLL_CAN_MS) {
        lastCanMs = now;
        pollCAN(telemetry);
    }

    if (now - lastPublishMs >= PUBLISH_MS) {
        lastPublishMs = now;
        publishTelemetry(telemetry);
    }
}
