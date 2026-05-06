#pragma once
#include <ArduinoJson.h>
#include <PubSubClient.h>

void readFirmwareVersion(PubSubClient& mqtt);
void inverterInit();
void verifyAndReinit();
void pollModbus(JsonDocument& telemetry);
