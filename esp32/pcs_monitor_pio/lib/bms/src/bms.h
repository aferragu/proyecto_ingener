#pragma once
#include <Arduino.h>
#include <ModbusMaster.h>
#include <ArduinoJson.h>
#include "bms_parser.h"

extern BmsData bmsData;

void bmsInit(HardwareSerial& serial, uint8_t deRePin);
void pollBMS(JsonDocument& telemetry);
