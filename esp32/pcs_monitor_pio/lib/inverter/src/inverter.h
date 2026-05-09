#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

void inverterInit(HardwareSerial& serial, uint8_t deRePin);
void readFirmwareVersion(PubSubClient& mqtt);
void verifyAndReinit();
void pollModbus(JsonDocument& telemetry);
bool inverterSetPower(float kw);
bool inverterPowerOn();
bool inverterShutdown();
