#pragma once
#include <PubSubClient.h>
#include <ArduinoJson.h>

extern PubSubClient mqttClient;

void connectWiFi();
void connectMQTT();
void publishTelemetry(JsonDocument& telemetry);
void onRpcMessage(char* topic, byte* payload, unsigned int length);
void updateLed();
