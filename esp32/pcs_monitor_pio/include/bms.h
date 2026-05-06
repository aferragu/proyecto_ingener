#pragma once
#include "bms_core.h"
#include <ArduinoJson.h>

extern BmsData bmsData;

void initCAN();
void initBMS();
void pollCAN(JsonDocument& telemetry);
