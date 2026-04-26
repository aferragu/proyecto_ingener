#pragma once
#include <ArduinoJson.h>

void initCAN();
void pollCAN(JsonDocument& telemetry);
