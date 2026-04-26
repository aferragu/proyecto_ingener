#pragma once
#include <ArduinoJson.h>

void readFirmwareVersion(PubSubClient& mqtt);
void inverterInit();
void verifyAndReinit();
void pollModbus(JsonDocument& telemetry);
